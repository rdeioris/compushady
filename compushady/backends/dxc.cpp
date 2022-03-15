#include <Python.h>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <comdef.h>
#include <d3dcompiler.h>
#endif

#include "dxcapi.h"

#include "compushady.h"

#include "spirv_cross/spirv_msl.hpp"

static PyObject* dxc_generate_exception(HRESULT hr, const char* prefix)
{
#ifdef _WIN32
	_com_error err(hr);
	return PyErr_Format(PyExc_Exception, "%s: %s\n", prefix, err.ErrorMessage());
#else
	return PyErr_Format(PyExc_Exception, "%s: error code %d\n", prefix, hr);
#endif
}

static PyObject* dxc_compile(PyObject* self, PyObject* args)
{
	Py_buffer view;
	PyObject* py_entry_point;
	int shader_binary_type;
	if (!PyArg_ParseTuple(args, "s*Ui", &view, &py_entry_point, &shader_binary_type))
		return NULL;

#ifdef _WIN32
	if (shader_binary_type == COMPUSHADY_SHADER_BINARY_TYPE_DXBC)
	{
		ID3DBlob* blob = NULL;
		ID3D10Blob* error_messages = NULL;
		const char* entry_point = PyUnicode_AsUTF8(py_entry_point);
		HRESULT hr = D3DCompile(view.buf, view.len, NULL, NULL, NULL, entry_point, "cs_5_0", 0, 0, &blob, &error_messages);
		if (hr != S_OK)
		{
			if (blob)
				blob->Release();
			if (error_messages)
			{
				PyObject* py_unicode_error = PyUnicode_FromStringAndSize((const char*)error_messages->GetBufferPointer(), error_messages->GetBufferSize());
				PyObject* py_exc = PyErr_Format(PyExc_Exception, "%U", py_unicode_error);
				Py_DECREF(py_unicode_error);

				if (error_messages)
					error_messages->Release();
				return py_exc;
			}
			return dxc_generate_exception(hr, "unable to compile shader");
		}
		PyObject* py_compiled_blob = PyBytes_FromStringAndSize((const char*)blob->GetBufferPointer(), blob->GetBufferSize());
		blob->Release();
		if (error_messages)
		{
			error_messages->Release();
		}
		return py_compiled_blob;
	}
#endif

	static DxcCreateInstanceProc dxcompiler_lib_create_instance_proc = NULL;

	if (!dxcompiler_lib_create_instance_proc)
	{
#ifdef _WIN32
		static HMODULE dxcompiler_lib = NULL;
		if (!dxcompiler_lib)
		{
			dxcompiler_lib = LoadLibraryExA("dxcompiler.dll", NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		}

		if (dxcompiler_lib)
		{
			dxcompiler_lib_create_instance_proc = (DxcCreateInstanceProc)GetProcAddress(dxcompiler_lib, "DxcCreateInstance");
		}
#else
		dxcompiler_lib_create_instance_proc = (DxcCreateInstanceProc)dlsym(RTLD_DEFAULT, "DxcCreateInstance");
#endif
	}

	if (!dxcompiler_lib_create_instance_proc)
	{
		return PyErr_Format(PyExc_Exception, "unable to load dxcompiler library");
	}

	IDxcLibrary* dxc_library;
	HRESULT hr = dxcompiler_lib_create_instance_proc(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void**)&dxc_library);
	if (hr != S_OK)
	{
		return dxc_generate_exception(hr, "unable to create DXC library instance");
	}

	// compile the shader
	IDxcCompiler* dxc_compiler;
	hr = dxcompiler_lib_create_instance_proc(CLSID_DxcCompiler, __uuidof(IDxcCompiler), (void**)&dxc_compiler);
	if (hr != S_OK)
	{
		dxc_library->Release();
		return dxc_generate_exception(hr, "unable to create DXC compiler instance");
	}

	IDxcBlobEncoding* blob_source;
	hr = dxc_library->CreateBlobWithEncodingOnHeapCopy(view.buf, (UINT32)view.len, CP_UTF8, &blob_source);

	if (hr != S_OK)
	{
		dxc_compiler->Release();
		dxc_library->Release();
		return dxc_generate_exception(hr, "unable to create DXC blob");
	}

	wchar_t* entry_point = PyUnicode_AsWideCharString(py_entry_point, NULL);
	if (!entry_point)
	{
		blob_source->Release();
		dxc_compiler->Release();
		dxc_library->Release();
		return NULL;
	}

	std::vector<const wchar_t*> arguments;
	if (shader_binary_type == COMPUSHADY_SHADER_BINARY_TYPE_SPIRV || shader_binary_type == COMPUSHADY_SHADER_BINARY_TYPE_MSL || shader_binary_type == COMPUSHADY_SHADER_BINARY_TYPE_GLSL)
	{
		arguments.push_back(L"-spirv");
		arguments.push_back(L"-fvk-t-shift");
		arguments.push_back(L"1024");
		arguments.push_back(L"0");
		arguments.push_back(L"-fvk-u-shift");
		arguments.push_back(L"2048");
		arguments.push_back(L"0");
	}

	IDxcOperationResult* result;
	hr = dxc_compiler->Compile(blob_source, NULL, entry_point, L"cs_6_0", arguments.data(), (UINT32)arguments.size(), NULL, 0, NULL, &result);
	if (hr == S_OK)
	{
		result->GetStatus(&hr);
	}

	if (hr != S_OK)
	{
		if (result)
		{
			IDxcBlobEncoding* blob_error;
			if (result->GetErrorBuffer(&blob_error) == S_OK)
			{
				PyObject* py_unicode_error = PyUnicode_FromStringAndSize((const char*)blob_error->GetBufferPointer(), blob_error->GetBufferSize());
				PyErr_Format(PyExc_ValueError, "%U", py_unicode_error);
				Py_DECREF(py_unicode_error);
			}
			result->Release();
		}

		if (!PyErr_Occurred())
		{
			dxc_generate_exception(hr, "Unable to compile HLSl shader");
		}

		blob_source->Release();
		dxc_compiler->Release();
		dxc_library->Release();
		return NULL;
	}

	IDxcBlob* compiled_blob = NULL;
	result->GetResult(&compiled_blob);

#ifdef _WIN32
	static HMODULE dxil = NULL;
	if (!dxil)
	{
		dxil = LoadLibraryExA("dxil.dll", NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	}

	if (dxil)
	{
		DxcCreateInstanceProc dxil_create_instance_proc = (DxcCreateInstanceProc)GetProcAddress(dxil, "DxcCreateInstance");
		if (dxil_create_instance_proc)
		{
			IDxcValidator* validator;
			hr = dxil_create_instance_proc(CLSID_DxcValidator, __uuidof(IDxcValidator), (void**)&validator);
			if (hr == S_OK)
			{
				IDxcOperationResult* verify_result;
				validator->Validate(compiled_blob, DxcValidatorFlags_InPlaceEdit, &verify_result);
				if (verify_result)
				{
					verify_result->Release();
				}
				validator->Release();
			}
		}
	}
#endif
	PyObject* py_compiled_blob = NULL;
	PyObject* py_exc = NULL;
	if (shader_binary_type == COMPUSHADY_SHADER_BINARY_TYPE_MSL)
	{
		try
		{
			spirv_cross::CompilerMSL msl((uint32_t*)compiled_blob->GetBufferPointer(), compiled_blob->GetBufferSize() / 4);
			spirv_cross::CompilerMSL::Options options;
			options.enable_decoration_binding = true;
			msl.set_msl_options(options);
			std::string msl_code = msl.compile();
			py_compiled_blob = PyBytes_FromStringAndSize(msl_code.data(), msl_code.length());
		}
		catch (const std::exception& e)
		{
			py_exc = PyErr_Format(PyExc_Exception, "SPIRV-Cross: %s", e.what());
		}
	}
	else if (shader_binary_type == COMPUSHADY_SHADER_BINARY_TYPE_GLSL)
	{
		try
		{
			spirv_cross::CompilerGLSL glsl((uint32_t*)compiled_blob->GetBufferPointer(), compiled_blob->GetBufferSize() / 4);
			std::string glsl_code = glsl.compile();
			py_compiled_blob = PyBytes_FromStringAndSize(glsl_code.data(), glsl_code.length());
		}
		catch (const std::exception& e)
		{
			py_exc = PyErr_Format(PyExc_Exception, "SPIRV-Cross: %s", e.what());
		}
	}
	else
	{
		py_compiled_blob = PyBytes_FromStringAndSize((const char*)compiled_blob->GetBufferPointer(), compiled_blob->GetBufferSize());
	}

	compiled_blob->Release();
	result->Release();
	blob_source->Release();
	dxc_compiler->Release();
	dxc_library->Release();

	if (py_exc)
	{
		if (py_compiled_blob)
		{
			Py_DECREF(py_compiled_blob);
		}
		return py_exc;
	}

	return py_compiled_blob;
}

static PyMethodDef compushady_backends_dxc_methods[] = {
	{"compile", (PyCFunction)dxc_compile, METH_VARARGS, "Compile an HLSL shader"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static struct PyModuleDef compushady_backends_dxc_module = {
	PyModuleDef_HEAD_INIT,
	"dxc",
	NULL,
	-1,
	compushady_backends_dxc_methods
};

PyMODINIT_FUNC
PyInit_dxc(void)
{
	return PyModule_Create(&compushady_backends_dxc_module);
}

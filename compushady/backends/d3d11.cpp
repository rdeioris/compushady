#include <Python.h>
#include "structmember.h"
#include <d3d11.h>
#include <dxgi1_3.h>
#include <comdef.h>
#include <vector>

#define HEAP_DEFAULT 0
#define HEAP_UPLOAD 1
#define HEAP_READBACK 2

#define ADD_FORMAT(x) PyModule_AddObject(m, "FORMAT_" #x, PyLong_FromLongLong(DXGI_FORMAT_##x))

typedef struct d3d11_Device
{
	PyObject_HEAD;
	IDXGIAdapter1* adapter;
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	PyObject* name;
	SIZE_T dedicated_video_memory;
	SIZE_T dedicated_system_memory;
	SIZE_T shared_system_memory;
	UINT vendor_id;
	UINT device_id;
	char is_hardware;
} d3d12_Device;

typedef struct d3d11_Resource
{
	PyObject_HEAD;
	ID3D11Resource* resource;
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	SIZE_T size;
} d3d11_Resource;

typedef struct d3d11_Compute
{
	PyObject_HEAD;
	ID3D11Device* device;
	ID3D11DeviceContext* context;
} d3d11_Compute;

static PyObject* d3d11_generate_exception(HRESULT hr, const char* prefix)
{
	_com_error err(hr);
	return PyErr_Format(PyExc_Exception, "%s: %s\n", prefix, err.ErrorMessage());
}

static PyMemberDef d3d11_Device_members[] = {
	{"name", T_OBJECT_EX, offsetof(d3d12_Device, name), 0, "device name/description"},
	{"dedicated_video_memory", T_ULONGLONG, offsetof(d3d12_Device, dedicated_video_memory), 0, "device dedicated video memory amount"},
	{"dedicated_system_memory", T_ULONGLONG, offsetof(d3d12_Device, dedicated_system_memory), 0, "device dedicated system memory amount"},
	{"shared_system_memory", T_ULONGLONG, offsetof(d3d12_Device, shared_system_memory), 0, "device shared system memory amount"},
	{"vendor_id", T_UINT, offsetof(d3d12_Device, vendor_id), 0, "device VendorId"},
	{"device_id", T_UINT, offsetof(d3d12_Device, vendor_id), 0, "device DeviceId"},
	{"is_hardware", T_BOOL, offsetof(d3d12_Device, is_hardware), 0, "returns True if this is a hardware device and not a emulated/software one"},
	{NULL}  /* Sentinel */
};

static void d3d11_Device_dealloc(d3d12_Device* self)
{
	Py_XDECREF(self->name);

	if (self->context)
		self->context->Release();

	if (self->device)
		self->device->Release();

	if (self->adapter)
		self->adapter->Release();

	Py_TYPE(self)->tp_free((PyObject*)self);
}

ID3D11Device* d3d11_Device_get_device(d3d12_Device* self)
{
	if (self->device)
		return self->device;

	HRESULT hr = D3D11CreateDevice(self->adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &self->device, NULL, &self->context);
	if (hr != S_OK)
	{
		d3d11_generate_exception(hr, "Unable to create ID3D11Device");
		return NULL;
	}

	return self->device;
}


static PyTypeObject d3d11_Device_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.d3d11.Device", /* tp_name */
	sizeof(d3d11_Device),                                              /* tp_basicsize */
	0,                                                                 /* tp_itemsize */
	(destructor)d3d11_Device_dealloc,                                  /* tp_dealloc */
	0,                                                                 /* tp_print */
	0,                                                                 /* tp_getattr */
	0,                                                                 /* tp_setattr */
	0,                                                                 /* tp_reserved */
	0,                                                                 /* tp_repr */
	0,                                                                 /* tp_as_number */
	0,                                                                 /* tp_as_sequence */
	0,                                                                 /* tp_as_mapping */
	0,                                                                 /* tp_hash  */
	0,                                                                 /* tp_call */
	0,                                                                 /* tp_str */
	0,                                                                 /* tp_getattro */
	0,                                                                 /* tp_setattro */
	0,                                                                 /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,                                                /* tp_flags */
	"compushady d3d11 Device",                                         /* tp_doc */
};

static void d3d11_Resource_dealloc(d3d11_Resource* self)
{
	if (self->resource)
	{
		self->resource->Release();
	}

	if (self->device)
	{
		self->device->Release();
	}

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject d3d11_Resource_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.d3d11.Resource", /* tp_name */
	sizeof(d3d11_Resource),                                              /* tp_basicsize */
	0,																	 /* tp_itemsize */
	(destructor)d3d11_Resource_dealloc,                                  /* tp_dealloc */
	0,                                                                   /* tp_print */
	0,                                                                   /* tp_getattr */
	0,                                                                   /* tp_setattr */
	0,                                                                   /* tp_reserved */
	0,                                                                   /* tp_repr */
	0,                                                                   /* tp_as_number */
	0,                                                                   /* tp_as_sequence */
	0,                                                                   /* tp_as_mapping */
	0,                                                                   /* tp_hash  */
	0,                                                                   /* tp_call */
	0,                                                                   /* tp_str */
	0,                                                                   /* tp_getattro */
	0,                                                                   /* tp_setattro */
	0,                                                                   /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,                                                  /* tp_flags */
	"compushady d3d11 Resource",                                         /* tp_doc */
};

static void d3d11_Compute_dealloc(d3d11_Compute* self)
{
	if (self->context)
		self->context->Release();
	if (self->device)
		self->device->Release();

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject d3d11_Compute_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.d3d11.Compute",  /* tp_name */
	sizeof(d3d11_Compute),                                               /* tp_basicsize */
	0,																	 /* tp_itemsize */
	(destructor)d3d11_Compute_dealloc,                                   /* tp_dealloc */
	0,                                                                   /* tp_print */
	0,                                                                   /* tp_getattr */
	0,                                                                   /* tp_setattr */
	0,                                                                   /* tp_reserved */
	0,                                                                   /* tp_repr */
	0,                                                                   /* tp_as_number */
	0,                                                                   /* tp_as_sequence */
	0,                                                                   /* tp_as_mapping */
	0,                                                                   /* tp_hash  */
	0,                                                                   /* tp_call */
	0,                                                                   /* tp_str */
	0,                                                                   /* tp_getattro */
	0,                                                                   /* tp_setattro */
	0,                                                                   /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,                                                  /* tp_flags */
	"compushady d3d11 Compute",                                          /* tp_doc */
};

static PyObject* d3d11_Device_create_buffer(d3d12_Device* self, PyObject* args)
{
	int heap;
	SIZE_T size;
	if (!PyArg_ParseTuple(args, "iK", &heap, &size))
		return NULL;

	ID3D11Device* device = d3d11_Device_get_device(self);
	if (!device)
		return NULL;

	D3D11_BUFFER_DESC buffer_desc = {};
	UINT cpu_access = 0;

	switch (heap)
	{
	case HEAP_DEFAULT:
		buffer_desc.Usage = D3D11_USAGE_DEFAULT;
		break;
	case HEAP_UPLOAD:
		buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
		cpu_access = D3D11_CPU_ACCESS_WRITE;
		break;
	case HEAP_READBACK:
		buffer_desc.Usage = D3D11_USAGE_STAGING;
		cpu_access = D3D11_CPU_ACCESS_READ;
		break;
	default:
		return PyErr_Format(PyExc_ValueError, "invalid heap type: %d", heap);
	}

	buffer_desc.ByteWidth = size;

	ID3D11Buffer* buffer;
	HRESULT hr = device->CreateBuffer(&buffer_desc, NULL, &buffer);
	if (hr != S_OK)
	{
		return d3d11_generate_exception(hr, "Unable to create ID3D12Resource1");
	}

	d3d11_Resource* py_resource = (d3d11_Resource*)PyObject_New(d3d11_Resource, &d3d11_Resource_Type);
	if (!py_resource)
	{
		buffer->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}

	py_resource->resource = buffer;
	py_resource->device = device;
	py_resource->device->AddRef();
	py_resource->size = size;

	return (PyObject*)py_resource;
}

static PyObject* d3d11_Device_create_texture2d(d3d11_Device* self, PyObject* args)
{
	UINT width;
	UINT height;
	DXGI_FORMAT format;
	PyObject* py_uav = NULL;
	if (!PyArg_ParseTuple(args, "IIiO", &width, &height, &format, &py_uav))
		return NULL;

	ID3D11Device* device = d3d11_Device_get_device(self);
	if (!device)
		return NULL;

	D3D11_TEXTURE2D_DESC texture2d_desc = {};
	texture2d_desc.Width = width;
	texture2d_desc.Height = height;
	texture2d_desc.Usage = D3D11_USAGE_DEFAULT;
	texture2d_desc.ArraySize = 1;
	texture2d_desc.MipLevels = 1;
	texture2d_desc.Format = format;
	texture2d_desc.SampleDesc.Count = 1;

	ID3D11Texture2D* texture;
	HRESULT hr = device->CreateTexture2D(&texture2d_desc, NULL, &texture);
	if (hr != S_OK)
	{
		return d3d11_generate_exception(hr, "Unable to create ID3D12Resource1");
	}

	d3d11_Resource* py_resource = (d3d11_Resource*)PyObject_New(d3d11_Resource, &d3d11_Resource_Type);
	if (!py_resource)
	{
		texture->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}

	py_resource->resource = texture;
	py_resource->device = device;
	py_resource->device->AddRef();

	return (PyObject*)py_resource;
}

static PyObject* d3d11_Device_get_debug_messages(d3d12_Device* self, PyObject* args)
{
	ID3D11Device* device = d3d11_Device_get_device(self);
	if (!device)
		return NULL;

	PyObject* py_list = PyList_New(0);

	ID3D11InfoQueue* queue;
	HRESULT hr = self->device->QueryInterface<ID3D11InfoQueue>(&queue);
	if (hr == S_OK)
	{
		UINT64 num_of_messages = queue->GetNumStoredMessages();
		for (UINT64 i = 0; i < num_of_messages; i++)
		{
			SIZE_T message_size = 0;
			queue->GetMessage(i, NULL, &message_size);
			D3D11_MESSAGE* message = (D3D11_MESSAGE*)PyMem_Malloc(message_size);
			queue->GetMessage(i, message, &message_size);
			PyObject* py_debug_message = PyUnicode_FromStringAndSize(message->pDescription, message->DescriptionByteLength);
			PyMem_Free(message);
			PyList_Append(py_list, py_debug_message);
			Py_DECREF(py_debug_message);
		}
		queue->ClearStoredMessages();
	}

	return py_list;
}

static PyObject* d3d11_Device_create_compute(d3d12_Device* self, PyObject* args, PyObject* kwds)
{
	const char* kwlist[] = { "shader", "cbv", "srv", "uav", NULL };
	Py_buffer view;
	PyObject* py_cbv = NULL;
	PyObject* py_srv = NULL;
	PyObject* py_uav = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*|OOO", (char**)kwlist,
		&view, &py_cbv, &py_srv, &py_uav))
		return NULL;

	ID3D11Device* device = d3d11_Device_get_device(self);
	if (!device)
		return NULL;

	if (py_cbv)
	{
		PyObject* py_iter = PyObject_GetIter(py_cbv);
		if (!py_iter)
			return NULL;
		while (PyObject* py_item = PyIter_Next(py_iter))
		{
			int ret = PyObject_IsInstance(py_item, (PyObject*)&d3d11_Resource_Type);
			if (ret < 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return NULL;
			}
			else if (ret == 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
			}
			//cbv.push_back((d3d12_Resource*)py_item);
			Py_DECREF(py_item);
		}
		Py_DECREF(py_iter);
	}

	if (py_srv)
	{
		PyObject* py_iter = PyObject_GetIter(py_srv);
		if (!py_iter)
			return NULL;
		while (PyObject* py_item = PyIter_Next(py_iter))
		{
			int ret = PyObject_IsInstance(py_item, (PyObject*)&d3d11_Resource_Type);
			if (ret < 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return NULL;
			}
			else if (ret == 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
			}
			//srv.push_back((d3d12_Resource*)py_item);
			Py_DECREF(py_item);
		}
		Py_DECREF(py_iter);
	}

	if (py_uav)
	{
		PyObject* py_iter = PyObject_GetIter(py_uav);
		if (!py_iter)
			return NULL;
		while (PyObject* py_item = PyIter_Next(py_iter))
		{
			int ret = PyObject_IsInstance(py_item, (PyObject*)&d3d11_Resource_Type);
			if (ret < 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return NULL;
			}
			else if (ret == 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
			}
			//uav.push_back((d3d12_Resource*)py_item);
			Py_DECREF(py_item);
		}
		Py_DECREF(py_iter);
	}

	d3d11_Compute* py_compute = (d3d11_Compute*)PyObject_New(d3d11_Compute, &d3d11_Compute_Type);
	if (!py_compute)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d11 Compute");
	}
	py_compute->device = device;
	py_compute->device->AddRef();
	py_compute->context = self->context;
	py_compute->context->AddRef();

	ID3D11ComputeShader* compute_shader;
	device->CreateComputeShader(view.buf, view.len, NULL, &compute_shader);

	self->context->CSSetShader(compute_shader, NULL, 0);
	//self->context->CSSetConstantBuffers(0, );
	return (PyObject*)py_compute;
}

static PyMethodDef d3d11_Device_methods[] = {
	{"create_buffer", (PyCFunction)d3d11_Device_create_buffer, METH_VARARGS, "Creates a Buffer object"},
	{"create_texture2d", (PyCFunction)d3d11_Device_create_texture2d, METH_VARARGS, "Creates a Texture2D object"},
	{"get_debug_messages", (PyCFunction)d3d11_Device_get_debug_messages, METH_VARARGS, "Get Device's debug messages"},
	{"create_compute", (PyCFunction)d3d11_Device_create_compute, METH_VARARGS | METH_KEYWORDS, "Creates a Compute object"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* d3d11_Resource_upload(d3d11_Resource* self, PyObject* args)
{
	Py_buffer view;
	UINT sub_resource = 0;
	if (!PyArg_ParseTuple(args, "Iy*", &sub_resource, &view))
		return NULL;

	if ((SIZE_T)view.len > self->size)
	{
		size_t size = view.len;
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_ValueError, "supplied buffer is bigger than resource size: %llu (expected no more than %llu)", size, self->size);
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = self->context->Map(self->resource, sub_resource, D3D11_MAP_WRITE, 0, &mapped);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		return d3d11_generate_exception(hr, "Unable to Map() ID3D11Resource");
	}

	memcpy(mapped.pData, view.buf, view.len);
	self->context->Unmap(self->resource, sub_resource);
	PyBuffer_Release(&view);

	Py_RETURN_NONE;
}

static PyObject* d3d11_Resource_readback(d3d11_Resource* self, PyObject* args)
{
	UINT sub_resource = 0;
	SIZE_T size;
	if (!PyArg_ParseTuple(args, "IK", &sub_resource, &size))
		return NULL;

	if (size > self->size)
	{
		return PyErr_Format(PyExc_ValueError, "requested buffer size is bigger than resource size: %llu (expected no more than %llu)", size, self->size);
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = self->context->Map(self->resource, sub_resource, D3D11_MAP_READ, 0, &mapped);
	if (hr != S_OK)
	{
		return d3d11_generate_exception(hr, "Unable to Map() ID3D11Resource");
	}

	PyObject* py_bytes = PyBytes_FromStringAndSize((const char*)mapped.pData, size);
	self->context->Unmap(self->resource, sub_resource);
	return py_bytes;
}

static PyObject* d3d11_Resource_copy_to(d3d11_Resource* self, PyObject* args)
{
	PyObject* py_destination;
	if (!PyArg_ParseTuple(args, "O", &py_destination))
		return NULL;

	int ret = PyObject_IsInstance(py_destination, (PyObject*)&d3d11_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}

	d3d11_Resource* dst_resource = (d3d11_Resource*)py_destination;
	SIZE_T dst_size = ((d3d11_Resource*)py_destination)->size;

	if (self->size > dst_size)
	{
		return PyErr_Format(PyExc_ValueError, "Resource size is bigger than destination size: %llu (expected no more than %llu)", self->size, dst_size);
	}

	//self->context->CopyResource();
	//self->context->CopySubresourceRegion();

	Py_RETURN_NONE;
}

static PyMethodDef d3d12_Resource_methods[] = {
	{"upload", (PyCFunction)d3d11_Resource_upload, METH_VARARGS, "Upload bytes to a GPU Resource"},
	{"readback", (PyCFunction)d3d11_Resource_readback, METH_VARARGS, "Readback bytes from a GPU Resource"},
	{"copy_to", (PyCFunction)d3d11_Resource_copy_to, METH_VARARGS, "Copy resource content to another resource"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* d3d11_Compute_dispatch(d3d11_Compute* self, PyObject* args)
{
	UINT x, y, z;
	if (!PyArg_ParseTuple(args, "III", &x, &y, &z))
		return NULL;

	self->context->Dispatch(x, y, z);

	Py_RETURN_NONE;
}

static PyMethodDef d3d11_Compute_methods[] = {
	{"dispatch", (PyCFunction)d3d11_Compute_dispatch, METH_VARARGS, "Execute a Compute Pipeline"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* d3d11_get_discovered_devices(PyObject* self)
{
	IDXGIFactory1* factory = NULL;
	HRESULT hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory1), (void**)&factory);
	if (hr != S_OK)
	{
		_com_error err(hr);
		return PyErr_Format(PyExc_Exception, "unable to create IDXGIFactory1: %s", err.ErrorMessage());
	}

	PyObject* py_list = PyList_New(0);

	UINT i = 0;
	IDXGIAdapter1* adapter;
	while (factory->EnumAdapters1(i++, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 adapter_desc;
		hr = adapter->GetDesc1(&adapter_desc);
		if (hr != S_OK)
		{
			Py_DECREF(py_list);
			_com_error err(hr);
			return PyErr_Format(PyExc_Exception, "error while calling GetDesc1: %s", err.ErrorMessage());
		}

		d3d11_Device* py_device = (d3d11_Device*)PyObject_New(d3d11_Device, &d3d11_Device_Type);
		if (!py_device)
		{
			Py_DECREF(py_list);
			return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d11 Device");
		}

		py_device->adapter = adapter; // kepp the com refcnt as is given that EnumAdapters1 increases it
		py_device->device = NULL;     // will be lazily allocated
		py_device->name = PyUnicode_FromWideChar(adapter_desc.Description, -1);
		py_device->dedicated_video_memory = adapter_desc.DedicatedVideoMemory;
		py_device->dedicated_system_memory = adapter_desc.DedicatedSystemMemory;
		py_device->shared_system_memory = adapter_desc.SharedSystemMemory;
		py_device->vendor_id = adapter_desc.VendorId;
		py_device->device_id = adapter_desc.DeviceId;
		py_device->is_hardware = (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0;

		PyList_Append(py_list, (PyObject*)py_device);
		Py_DECREF(py_device);
	}

	factory->Release();

	return py_list;
}

static PyMethodDef compushady_backends_d3d11_methods[] = {
	{"get_discovered_devices", (PyCFunction)d3d11_get_discovered_devices, METH_NOARGS, "Returns the list of discovered GPU devices"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static struct PyModuleDef compushady_backends_d3d11_module = {
	PyModuleDef_HEAD_INIT,
	"d3d11",
	NULL,
	-1,
	compushady_backends_d3d11_methods };

PyMODINIT_FUNC
PyInit_d3d11(void)
{
	PyObject* m = PyModule_Create(&compushady_backends_d3d11_module);
	if (m == NULL)
		return NULL;

	d3d11_Device_Type.tp_members = d3d11_Device_members;
	d3d11_Device_Type.tp_methods = d3d11_Device_methods;
	if (PyType_Ready(&d3d11_Device_Type) < 0)
	{
		Py_DECREF(m);
		return NULL;
	}
	Py_INCREF(&d3d11_Device_Type);
	if (PyModule_AddObject(m, "Device", (PyObject*)&d3d11_Device_Type) < 0)
	{
		Py_DECREF(&d3d11_Device_Type);
		Py_DECREF(m);
		return NULL;
	}

	d3d11_Resource_Type.tp_methods = d3d12_Resource_methods;
	if (PyType_Ready(&d3d11_Resource_Type) < 0)
	{
		Py_DECREF(&d3d11_Device_Type);
		Py_DECREF(m);
		return NULL;
	}
	Py_INCREF(&d3d11_Resource_Type);
	if (PyModule_AddObject(m, "Resource", (PyObject*)&d3d11_Resource_Type) < 0)
	{
		Py_DECREF(&d3d11_Resource_Type);
		Py_DECREF(&d3d11_Device_Type);
		Py_DECREF(m);
		return NULL;
	}

	d3d11_Compute_Type.tp_methods = d3d11_Compute_methods;
	if (PyType_Ready(&d3d11_Compute_Type) < 0)
	{
		Py_DECREF(&d3d11_Resource_Type);
		Py_DECREF(&d3d11_Device_Type);
		Py_DECREF(m);
		return NULL;
	}
	Py_INCREF(&d3d11_Compute_Type);
	if (PyModule_AddObject(m, "Compute", (PyObject*)&d3d11_Compute_Type) < 0)
	{
		Py_DECREF(&d3d11_Compute_Type);
		Py_DECREF(&d3d11_Resource_Type);
		Py_DECREF(&d3d11_Device_Type);
		Py_DECREF(m);
		return NULL;
	}

	ADD_FORMAT(R32G32B32A32_FLOAT);
	ADD_FORMAT(R32G32B32A32_UINT);
	ADD_FORMAT(R32G32B32A32_SINT);
	ADD_FORMAT(R32G32B32_FLOAT);
	ADD_FORMAT(R32G32B32_UINT);
	ADD_FORMAT(R32G32B32_SINT);
	ADD_FORMAT(R16G16B16A16_FLOAT);
	ADD_FORMAT(R16G16B16A16_UNORM);
	ADD_FORMAT(R16G16B16A16_UINT);
	ADD_FORMAT(R16G16B16A16_SNORM);
	ADD_FORMAT(R16G16B16A16_SINT);
	ADD_FORMAT(R32G32_FLOAT);
	ADD_FORMAT(R32G32_UINT);
	ADD_FORMAT(R32G32_SINT);
	ADD_FORMAT(R8G8B8A8_UNORM);
	ADD_FORMAT(R8G8B8A8_UNORM_SRGB);
	ADD_FORMAT(R8G8B8A8_UINT);
	ADD_FORMAT(R8G8B8A8_SNORM);
	ADD_FORMAT(R8G8B8A8_SINT);
	ADD_FORMAT(R16G16_FLOAT);
	ADD_FORMAT(R16G16_UNORM);
	ADD_FORMAT(R16G16_UINT);
	ADD_FORMAT(R16G16_SNORM);
	ADD_FORMAT(R16G16_SINT);
	ADD_FORMAT(R32_FLOAT);
	ADD_FORMAT(R32_UINT);
	ADD_FORMAT(R32_SINT);
	ADD_FORMAT(R8G8_UNORM);
	ADD_FORMAT(R8G8_UINT);
	ADD_FORMAT(R8G8_SNORM);
	ADD_FORMAT(R8G8_SINT);
	ADD_FORMAT(R16_FLOAT);
	ADD_FORMAT(R16_UNORM);
	ADD_FORMAT(R16_UINT);
	ADD_FORMAT(R16_SNORM);
	ADD_FORMAT(R16_SINT);
	ADD_FORMAT(R8_UNORM);
	ADD_FORMAT(R8_UINT);
	ADD_FORMAT(R8_SNORM);
	ADD_FORMAT(R8_SINT);
	ADD_FORMAT(B8G8R8A8_UNORM);
	ADD_FORMAT(B8G8R8A8_UNORM_SRGB);

	return m;
}
#include "compushady.h"

PyObject *Compushady_BufferError = NULL;
PyObject *Compushady_Texture1DError = NULL;
PyObject *Compushady_Texture2DError = NULL;
PyObject *Compushady_Texture3DError = NULL;
PyObject *Compushady_SamplerError = NULL;
PyObject *Compushady_HeapError = NULL;

PyObject *compushady_backend_init(PyModuleDef *py_module_def,
								  PyTypeObject *device_type, PyMemberDef *device_members, PyMethodDef *device_methods,
								  PyTypeObject *resource_type, PyMemberDef *resource_members, PyMethodDef *resource_methods,
								  PyTypeObject *swapchain_type, PyMemberDef *swapchain_members, PyMethodDef *swapchain_methods,
								  PyTypeObject *compute_type, PyMemberDef *compute_members, PyMethodDef *compute_methods,
								  PyTypeObject *sampler_type, PyMemberDef *sampler_members, PyMethodDef *sampler_methods,
								  PyTypeObject *heap_type, PyMemberDef *heap_members, PyMethodDef *heap_methods)
{
	PyObject *py_compushady = PyImport_ImportModule("compushady");
	if (!py_compushady)
	{
		return NULL;
	}

	PyObject *py_compushady_dict = PyModule_GetDict(py_compushady);
	if (!py_compushady_dict)
	{
		Py_DECREF(py_compushady);
		return NULL;
	}

	Compushady_BufferError = PyDict_GetItemString(py_compushady_dict, "BufferException");
	if (!Compushady_BufferError)
	{
		Py_DECREF(py_compushady);
		return PyErr_Format(PyExc_ImportError, "Unable to find compushady.BufferException");
	}

	Compushady_Texture1DError = PyDict_GetItemString(py_compushady_dict, "Texture1DException");
	if (!Compushady_Texture1DError)
	{
		Py_DECREF(py_compushady);
		return PyErr_Format(PyExc_ImportError, "Unable to find compushady.Texture1DException");
	}

	Compushady_Texture2DError = PyDict_GetItemString(py_compushady_dict, "Texture2DException");
	if (!Compushady_Texture2DError)
	{
		Py_DECREF(py_compushady);
		return PyErr_Format(PyExc_ImportError, "Unable to find compushady.Texture2DException");
	}

	Compushady_Texture3DError = PyDict_GetItemString(py_compushady_dict, "Texture3DException");
	if (!Compushady_Texture3DError)
	{
		Py_DECREF(py_compushady);
		return PyErr_Format(PyExc_ImportError, "Unable to find compushady.Texture3DException");
	}

	Compushady_SamplerError = PyDict_GetItemString(py_compushady_dict, "SamplerException");
	if (!Compushady_SamplerError)
	{
		Py_DECREF(py_compushady);
		return PyErr_Format(PyExc_ImportError, "Unable to find compushady.SamplerException");
	}

	Compushady_HeapError = PyDict_GetItemString(py_compushady_dict, "HeapException");
	if (!Compushady_HeapError)
	{
		Py_DECREF(py_compushady);
		return PyErr_Format(PyExc_ImportError, "Unable to find compushady.HeapException");
	}

	PyObject *m = PyModule_Create(py_module_def);
	if (m == NULL)
		return NULL;

	device_type->tp_members = device_members;
	device_type->tp_methods = device_methods;
	if (PyType_Ready(device_type) < 0)
	{
		Py_DECREF(m);
		return NULL;
	}
	Py_INCREF(device_type);
	if (PyModule_AddObject(m, "Device", (PyObject *)device_type) < 0)
	{
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}

	resource_type->tp_members = resource_members;
	resource_type->tp_methods = resource_methods;
	if (PyType_Ready(resource_type) < 0)
	{
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}
	Py_INCREF(resource_type);
	if (PyModule_AddObject(m, "Resource", (PyObject *)resource_type) < 0)
	{
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}

	swapchain_type->tp_members = swapchain_members;
	swapchain_type->tp_methods = swapchain_methods;
	if (PyType_Ready(swapchain_type) < 0)
	{
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}
	Py_INCREF(swapchain_type);
	if (PyModule_AddObject(m, "Swapchain", (PyObject *)swapchain_type) < 0)
	{
		Py_DECREF(swapchain_type);
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}

	compute_type->tp_members = compute_members;
	compute_type->tp_methods = compute_methods;
	if (PyType_Ready(compute_type) < 0)
	{
		Py_DECREF(swapchain_type);
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}
	Py_INCREF(compute_type);
	if (PyModule_AddObject(m, "Pipeline", (PyObject *)compute_type) < 0)
	{
		Py_DECREF(compute_type);
		Py_DECREF(swapchain_type);
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}

	sampler_type->tp_members = sampler_members;
	sampler_type->tp_methods = sampler_methods;
	if (PyType_Ready(sampler_type) < 0)
	{
		Py_DECREF(compute_type);
		Py_DECREF(swapchain_type);
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}
	Py_INCREF(sampler_type);
	if (PyModule_AddObject(m, "Sampler", (PyObject *)sampler_type) < 0)
	{
		Py_DECREF(sampler_type);
		Py_DECREF(compute_type);
		Py_DECREF(swapchain_type);
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}

	heap_type->tp_members = heap_members;
	heap_type->tp_methods = heap_methods;
	if (PyType_Ready(heap_type) < 0)
	{
		Py_DECREF(sampler_type);
		Py_DECREF(compute_type);
		Py_DECREF(swapchain_type);
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}
	Py_INCREF(heap_type);
	if (PyModule_AddObject(m, "Heap", (PyObject *)heap_type) < 0)
	{
		Py_DECREF(heap_type);
		Py_DECREF(sampler_type);
		Py_DECREF(compute_type);
		Py_DECREF(swapchain_type);
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}

	if (PyModule_AddObject(m, "name", PyUnicode_FromString(py_module_def->m_name)) < 0)
	{
		Py_DECREF(heap_type);
		Py_DECREF(sampler_type);
		Py_DECREF(compute_type);
		Py_DECREF(swapchain_type);
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}

	return m;
}

size_t compushady_get_size_by_pitch(const size_t pitch, const size_t width, const size_t height, const size_t depth, const size_t bytes_per_pixel)
{
	const size_t rows = height * depth;
	if (rows > 1)
	{
		return (pitch * (rows - 1)) + (bytes_per_pixel * width);
	}
	return pitch;
}

bool compushady_check_copy_to(const bool src_is_buffer, const bool dst_is_buffer, const uint64_t size, const uint64_t src_offset, const uint64_t dst_offset, const uint64_t src_size, const uint64_t dst_size, const uint32_t src_x, const uint32_t src_y, const uint32_t src_z, const uint32_t src_slice, const uint32_t src_slices, const uint32_t dst_slice, const uint32_t dst_slices, const uint32_t src_width, const uint32_t src_height, const uint32_t src_depth, const uint32_t dst_width, const uint32_t dst_height, const uint32_t dst_depth, uint32_t *dst_x, uint32_t *dst_y, uint32_t *dst_z, uint32_t *width, uint32_t *height, uint32_t *depth)
{
	// buffer to buffer
	if (src_is_buffer && dst_is_buffer)
	{
		if (src_offset + size > src_size || dst_offset + size > dst_size)
		{
			PyErr_Format(PyExc_ValueError,
						 "Resource requested size to copy (%llu) is out of bounds "
						 "(src_size: %llu, src_offset: %llu, dst_size: %llu, dst_offset: %llu)",
						 size, src_size, src_offset, dst_size, dst_offset);
			return false;
		}
	}
	// buffer to texture
	else if (src_is_buffer && !dst_is_buffer)
	{
		*dst_x = 0;
		*dst_y = 0;
		*dst_z = 0;
		if (src_offset + size > src_size || size < dst_size || dst_slice >= dst_slices)
		{
			PyErr_Format(PyExc_ValueError,
						 "Resource requested size to copy (%llu) is out of bounds "
						 "(src_size: %llu, src_offset: %llu, dst_size: %llu, dst_width: %u, dst_height: %u, dst_depth: %u dst_slices: %u)",
						 size, size, src_offset, dst_size, dst_width, dst_height, dst_depth, dst_slices);
			return false;
		}
	}
	// texture to buffer
	else if (!src_is_buffer && dst_is_buffer)
	{
		*dst_x = 0;
		*dst_y = 0;
		*dst_z = 0;
		if (dst_offset + size > dst_size || size < src_size || src_slice > src_slices)
		{
			PyErr_Format(PyExc_ValueError,
						 "Resource requested size to copy (%llu) is out of bounds "
						 "(dst_size: %llu, dst_offset: %llu, src_size: %llu, src_width: %u, src_height: %u, src_depth: %u src_slices: %u)",
						 size, dst_size, src_offset, dst_size, src_width, src_height, src_depth, src_slices);
			return false;
		}
	}
	// texture to texture
	else
	{
		if (*width == 0)
		{
			*width = src_width;
		}

		if (*height == 0)
		{
			*height = src_height;
		}

		if (*depth == 0)
		{
			*depth = src_depth;
		}

		if (src_x + *width > src_width || src_y + *height > src_height || src_z + *depth > src_depth || *dst_x + *width > dst_width || *dst_y + *height > dst_height || *dst_z + *depth > dst_depth || src_slice >= src_slices || dst_slice >= dst_slices)
		{
			PyErr_Format(PyExc_ValueError,
						 "Resource requested size to copy (width: %u, height: %u, depth: %u) is out of bounds "
						 "(src_width: %u, src_height: %u, src_depth: %u src_slices: %u, dst_width: %u, dst_height: %u, dst_depth: %u, dst_slices: %u)",
						 *width, *height, *depth, src_width, src_height, src_depth, src_slices, dst_width, dst_height, dst_depth, dst_slices);
			return false;
		}
	}

	return true;
}
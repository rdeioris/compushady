#include "compushady.h"

PyObject* Compushady_BufferError = NULL;
PyObject* Compushady_Texture1DError = NULL;
PyObject* Compushady_Texture2DError = NULL;
PyObject* Compushady_Texture3DError = NULL;

PyObject* compushady_backend_init(PyModuleDef* py_module_def,
	PyTypeObject* device_type, PyMemberDef* device_members, PyMethodDef* device_methods,
	PyTypeObject* resource_type, PyMemberDef* resource_members, PyMethodDef* resource_methods,
	PyTypeObject* swapchain_type, PyMemberDef* swapchain_members, PyMethodDef* swapchain_methods,
	PyTypeObject* compute_type, PyMemberDef* compute_members, PyMethodDef* compute_methods)
{
	PyObject* py_compushady = PyImport_ImportModule("compushady");
	if (!py_compushady)
	{
		return NULL;
	}

	PyObject* py_compushady_dict = PyModule_GetDict(py_compushady);
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


	PyObject* m = PyModule_Create(py_module_def);
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
	if (PyModule_AddObject(m, "Device", (PyObject*)device_type) < 0)
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
	if (PyModule_AddObject(m, "Resource", (PyObject*)resource_type) < 0)
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
	if (PyModule_AddObject(m, "Swapchain", (PyObject*)swapchain_type) < 0)
	{
		Py_DECREF(swapchain_type);
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}

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
	if (PyModule_AddObject(m, "Compute", (PyObject*)compute_type) < 0)
	{
		Py_DECREF(compute_type);
		Py_DECREF(swapchain_type);
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}

	if (PyModule_AddObject(m, "name", PyUnicode_FromString(py_module_def->m_name)) < 0)
	{
		Py_DECREF(compute_type);
		Py_DECREF(swapchain_type);
		Py_DECREF(resource_type);
		Py_DECREF(device_type);
		Py_DECREF(m);
		return NULL;
	}

	return m;
}

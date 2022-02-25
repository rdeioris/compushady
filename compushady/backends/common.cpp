#include "compushady.h"

PyObject* compushady_backend_init(PyModuleDef* py_module_def,
	PyTypeObject* device_type, PyMemberDef* device_members, PyMethodDef* device_methods,
	PyTypeObject* resource_type, PyMemberDef* resource_members, PyMethodDef* resource_methods,
	PyTypeObject* swapchain_type, PyMemberDef* swapchain_members, PyMethodDef* swapchain_methods,
	PyTypeObject* compute_type, PyMemberDef* compute_members, PyMethodDef* compute_methods)
{
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

	return m;
}
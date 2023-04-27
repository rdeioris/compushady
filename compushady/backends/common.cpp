#include "compushady.h"

PyObject* Compushady_BufferError = NULL;
PyObject* Compushady_Texture1DError = NULL;
PyObject* Compushady_Texture2DError = NULL;
PyObject* Compushady_Texture3DError = NULL;
PyObject* Compushady_SamplerError = NULL;
PyObject* Compushady_HeapError = NULL;

void compushady_backend_desc_init(
    compushady_backend_desc* compushady_backend, const char* name, PyMethodDef* backend_methods)
{
    memset(compushady_backend, 0, sizeof(compushady_backend_desc_t));
    PyModuleDef py_module_def = { PyModuleDef_HEAD_INIT, name, NULL, -1, backend_methods };
    compushady_backend->py_module_def = py_module_def;
}

void compushady_backend_destroy(compushady_backend_desc* compushady_backend)
{
    Py_XDECREF(compushady_backend->heap_type);
    Py_XDECREF(compushady_backend->sampler_type);
    Py_XDECREF(compushady_backend->compute_type);
    Py_XDECREF(compushady_backend->swapchain_type);
    Py_XDECREF(compushady_backend->resource_type);
    Py_XDECREF(compushady_backend->device_type);
}

PyObject* compushady_backend_init(compushady_backend_desc* compushady_backend)
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

    PyObject* m = PyModule_Create(&compushady_backend->py_module_def);
    if (m == NULL)
        return NULL;

    compushady_backend->device_type->tp_members = compushady_backend->device_members;
    compushady_backend->device_type->tp_methods = compushady_backend->device_methods;
    if (PyType_Ready(compushady_backend->device_type) < 0)
    {
        Py_DECREF(m);
        return NULL;
    }
    Py_INCREF(compushady_backend->device_type);
    if (PyModule_AddObject(m, "Device", (PyObject*)compushady_backend->device_type) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }

    compushady_backend->resource_type->tp_members = compushady_backend->resource_members;
    compushady_backend->resource_type->tp_methods = compushady_backend->resource_methods;
    if (PyType_Ready(compushady_backend->resource_type) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }
    Py_INCREF(compushady_backend->resource_type);
    if (PyModule_AddObject(m, "Resource", (PyObject*)compushady_backend->resource_type) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }

    compushady_backend->swapchain_type->tp_members = compushady_backend->swapchain_members;
    compushady_backend->swapchain_type->tp_methods = compushady_backend->swapchain_methods;
    if (PyType_Ready(compushady_backend->swapchain_type) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }
    Py_INCREF(compushady_backend->swapchain_type);
    if (PyModule_AddObject(m, "Swapchain", (PyObject*)compushady_backend->swapchain_type) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }

    compushady_backend->compute_type->tp_members = compushady_backend->compute_members;
    compushady_backend->compute_type->tp_methods = compushady_backend->compute_methods;
    if (PyType_Ready(compushady_backend->compute_type) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }
    Py_INCREF(compushady_backend->compute_type);
    if (PyModule_AddObject(m, "Compute", (PyObject*)compushady_backend->compute_type) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }

    compushady_backend->sampler_type->tp_members = compushady_backend->sampler_members;
    compushady_backend->sampler_type->tp_methods = compushady_backend->sampler_methods;
    if (PyType_Ready(compushady_backend->sampler_type) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }
    Py_INCREF(compushady_backend->sampler_type);
    if (PyModule_AddObject(m, "Sampler", (PyObject*)compushady_backend->sampler_type) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }

    compushady_backend->heap_type->tp_members = compushady_backend->heap_members;
    compushady_backend->heap_type->tp_methods = compushady_backend->heap_methods;
    if (PyType_Ready(compushady_backend->heap_type) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }
    Py_INCREF(compushady_backend->heap_type);
    if (PyModule_AddObject(m, "Heap", (PyObject*)compushady_backend->heap_type) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }

    if (PyModule_AddObject(m, "name", PyUnicode_FromString(compushady_backend->py_module_def.m_name)) < 0)
    {
        compushady_backend_destroy(compushady_backend);
        Py_DECREF(m);
        Py_DECREF(py_compushady);
        return NULL;
    }

    return m;
}

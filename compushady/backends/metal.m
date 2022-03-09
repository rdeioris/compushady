#include "compushady.h"
#import <MetalKit/MetalKit.h>

typedef struct metal_Device
{
    PyObject_HEAD;
    id<MTLDevice> device;
    PyObject* name;
    size_t dedicated_video_memory;
    size_t dedicated_system_memory;
    size_t shared_system_memory;
    uint32_t device_id;
    uint32_t vendor_id;
    char is_hardware;
    char is_discrete;
} metal_Device;

typedef struct metal_Resource
{
    PyObject_HEAD;
    metal_Device* py_device;
    id<MTLBuffer> buffer;
    NSUInteger size;
    NSUInteger stride;
} metal_Resource;

static void metal_Resource_dealloc(metal_Resource* self)
{
    if (self->buffer)
    {
        [self->buffer release];
    }
    
    Py_XDECREF(self->py_device);
    
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject metal_Resource_Type = {
    PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.metal.Resource", /* tp_name */
    sizeof(metal_Resource),                                              /* tp_basicsize */
    0,                                                                      /* tp_itemsize */
    (destructor)metal_Resource_dealloc,                                  /* tp_dealloc */
    0,                                                                      /* tp_print */
    0,                                                                      /* tp_getattr */
    0,                                                                      /* tp_setattr */
    0,                                                                      /* tp_reserved */
    0,                                                                      /* tp_repr */
    0,                                                                      /* tp_as_number */
    0,                                                                      /* tp_as_sequence */
    0,                                                                      /* tp_as_mapping */
    0,                                                                      /* tp_hash  */
    0,                                                                      /* tp_call */
    0,                                                                      /* tp_str */
    0,                                                                      /* tp_getattro */
    0,                                                                      /* tp_setattro */
    0,                                                                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                                                      /* tp_flags */
    "compushady metal Resource",                                          /* tp_doc */
};



static void metal_Device_dealloc(metal_Device* self)
{
    Py_XDECREF(self->name);
    
    if (self->device)
    {
        [self->device release];
    }
    
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject metal_Device_Type = {
    PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.metal.Device", /* tp_name */
    sizeof(metal_Device),												/* tp_basicsize */
    0,																	/* tp_itemsize */
    (destructor)metal_Device_dealloc,									/* tp_dealloc */
    0,																	/* tp_print */
    0,																	/* tp_getattr */
    0,																	/* tp_setattr */
    0,																	/* tp_reserved */
    0,																	/* tp_repr */
    0,																	/* tp_as_number */
    0,																	/* tp_as_sequence */
    0,																	/* tp_as_mapping */
    0,																	/* tp_hash  */
    0,																	/* tp_call */
    0,																	/* tp_str */
    0,																	/* tp_getattro */
    0,																	/* tp_setattro */
    0,																	/* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,													/* tp_flags */
    "compushady metal Device",											/* tp_doc */
};

static PyMemberDef metal_Device_members[] = {
    {"name", T_OBJECT_EX, offsetof(metal_Device, name), 0, "device name/description"},
    {"dedicated_video_memory", T_ULONGLONG, offsetof(metal_Device, dedicated_video_memory), 0, "device dedicated video memory amount"},
    {"dedicated_system_memory", T_ULONGLONG, offsetof(metal_Device, dedicated_system_memory), 0, "device dedicated system memory amount"},
    {"shared_system_memory", T_ULONGLONG, offsetof(metal_Device, shared_system_memory), 0, "device shared system memory amount"},
    {"vendor_id", T_UINT, offsetof(metal_Device, vendor_id), 0, "device VendorId"},
    {"device_id", T_UINT, offsetof(metal_Device, vendor_id), 0, "device DeviceId"},
    {"is_hardware", T_BOOL, offsetof(metal_Device, is_hardware), 0, "returns True if this is a hardware device and not an emulated/software one"},
    {"is_discrete", T_BOOL, offsetof(metal_Device, is_discrete), 0, "returns True if this is a discrete device"},
    {NULL} /* Sentinel */
};

static PyObject* compushady_create_metal_layer(PyObject* self, PyObject* args)
{
    NSWindow* window_handle;
    if (!PyArg_ParseTuple(args, "K", &window_handle))
        return NULL;
    
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    CAMetalLayer* metal_layer = [CAMetalLayer layer];
    metal_layer.device = device;
    metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metal_layer.framebufferOnly = YES;
    metal_layer.frame = [window_handle.contentView frame];
    window_handle.contentView.layer = metal_layer;
    
    return PyLong_FromUnsignedLongLong((unsigned long long)metal_layer);
}

static metal_Device* metal_Device_get_device(metal_Device* self)
{
    return self;
}

static PyObject* metal_Device_create_buffer(metal_Device * self, PyObject * args)
{
    int heap;
    size_t size;
    uint32_t stride;
    int format;
    if (!PyArg_ParseTuple(args, "iKIi", &heap, &size, &stride, &format))
        return NULL;
    
    if (!size)
        return PyErr_Format(PyExc_Exception, "zero size buffer");
    
    metal_Device* py_device = metal_Device_get_device(self);
    if (!py_device)
        return NULL;
    
    MTLResourceOptions options = MTLResourceStorageModePrivate;
    
    switch (heap)
    {
        case COMPUSHADY_HEAP_DEFAULT:
            break;
        case COMPUSHADY_HEAP_UPLOAD:
            options = MTLResourceStorageModeShared;
            break;
        case COMPUSHADY_HEAP_READBACK:
            options = MTLResourceStorageModeShared;
            break;
        default:
            return PyErr_Format(PyExc_Exception, "Invalid heap type: %d", heap);
    }
    
    id<MTLBuffer> buffer = [py_device->device newBufferWithLength:size options:options];
    
    metal_Resource* py_resource = (metal_Resource*)PyObject_New(metal_Resource, &metal_Resource_Type);
    if (!py_resource)
    {
        return PyErr_Format(PyExc_MemoryError, "unable to allocate vulkan Buffer");
    }
    COMPUSHADY_CLEAR(py_resource);
    py_resource->py_device = py_device;
    Py_INCREF(py_resource->py_device);
    
    py_resource->buffer = buffer;
    py_resource->size = size;
    py_resource->stride = stride;
    
    return (PyObject*)py_resource;
}


static PyMethodDef metal_Device_methods[] = {
    {"create_buffer", (PyCFunction)metal_Device_create_buffer, METH_VARARGS, "Creates a Buffer object"},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* metal_Resource_upload(metal_Resource* self, PyObject* args)
{
    Py_buffer view;
    size_t offset = 0;
    if (!PyArg_ParseTuple(args, "y*K", &view, &offset))
        return NULL;
    
    if ((size_t)offset + view.len > self->size)
    {
        size_t size = view.len;
        PyBuffer_Release(&view);
        return PyErr_Format(PyExc_ValueError, "supplied buffer is bigger than resource size: (offset %llu) %llu (expected no more than %llu)", offset, size, self->size);
    }
    
    char* mapped_data = (char*)[self->buffer contents];
    
    memcpy(mapped_data + offset, view.buf, view.len);
    
    PyBuffer_Release(&view);
    
    Py_RETURN_NONE;
}

static PyObject* metal_Resource_readback(metal_Resource * self, PyObject * args)
{
    size_t size;
    size_t offset;
    if (!PyArg_ParseTuple(args, "KK", &size, &offset))
        return NULL;

    if (size == 0)
        size = self->size - offset;

    if (offset + size > self->size)
    {
        return PyErr_Format(PyExc_ValueError, "requested buffer out of bounds: (offset %llu) %llu (expected no more than %llu)", offset, size, self->size);
    }

    char* mapped_data = (char*)[self->buffer contents];

    PyObject* py_bytes = PyBytes_FromStringAndSize(mapped_data + offset, size);
  
    return py_bytes;
}


static PyMethodDef metal_Resource_methods[] = {
    {"upload", (PyCFunction)metal_Resource_upload, METH_VARARGS, "Upload bytes to a GPU Resource"},
    {"readback", (PyCFunction)metal_Resource_readback, METH_VARARGS, "Readback bytes from a GPU Resource"},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* metal_get_discovered_devices(PyObject * self)
{
    PyObject* py_list = PyList_New(0);
    
    NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
    
    for (id device in devices)
    {
        metal_Device* py_device = (metal_Device*)PyObject_New(metal_Device, &metal_Device_Type);
        if (!py_device)
        {
            Py_DECREF(py_list);
            return PyErr_Format(PyExc_MemoryError, "unable to allocate metal Device");
        }
        COMPUSHADY_CLEAR(py_device);
        
        [device retain];
        py_device->device = device;
        py_device->name = PyUnicode_FromString([[device name] UTF8String]);
        py_device->dedicated_video_memory = [device hasUnifiedMemory] ? 0 : [device recommendedMaxWorkingSetSize];
        py_device->shared_system_memory = [device hasUnifiedMemory] ? [device recommendedMaxWorkingSetSize] : 0;
        py_device->is_hardware = [device location] != MTLDeviceLocationUnspecified;
        py_device->is_discrete = ![device hasUnifiedMemory];
        PyList_Append(py_list, (PyObject*)py_device);
        Py_DECREF(py_device);
    }
    
    [devices release];
    
    return py_list;
}

static PyMethodDef compushady_backends_metal_methods[] = {
    {"create_metal_layer", (PyCFunction)compushady_create_metal_layer, METH_VARARGS, "Creates a CAMetalLayer on the default device"},
    {"get_discovered_devices", (PyCFunction)metal_get_discovered_devices, METH_NOARGS, "Returns the list of discovered GPU devices"},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

static struct PyModuleDef compushady_backends_metal_module = {
    PyModuleDef_HEAD_INIT,
    "metal",
    NULL,
    -1,
    compushady_backends_metal_methods };

PyMODINIT_FUNC
PyInit_metal(void)
{
    PyObject* m = PyModule_Create(&compushady_backends_metal_module);
    if (!m)
        return NULL;
    
    metal_Device_Type.tp_members = metal_Device_members;
    metal_Device_Type.tp_methods = metal_Device_methods;
    if (PyType_Ready(&metal_Device_Type) < 0)
    {
        Py_DECREF(m);
        return NULL;
    }
    Py_INCREF(&metal_Device_Type);
    if (PyModule_AddObject(m, "Device", (PyObject*)&metal_Device_Type) < 0)
    {
        Py_DECREF(&metal_Device_Type);
        Py_DECREF(m);
        return NULL;
    }
    
    //metal_Resource_Type.tp_members = metal_Resource_members;
    metal_Resource_Type.tp_methods = metal_Resource_methods;
    if (PyType_Ready(&metal_Resource_Type) < 0)
    {
        Py_DECREF(m);
        return NULL;
    }
    Py_INCREF(&metal_Resource_Type);
    if (PyModule_AddObject(m, "Resource", (PyObject*)&metal_Resource_Type) < 0)
    {
        Py_DECREF(&metal_Resource_Type);
        Py_DECREF(m);
        return NULL;
    }
    
    return m;
}

#include "compushady.h"
#import <MetalKit/MetalKit.h>

typedef struct metal_Device
{
    PyObject_HEAD;
    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
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

typedef struct metal_Compute
{
    PyObject_HEAD;
    metal_Device* py_device;
    id<MTLCommandBuffer> compute_command_buffer;
    id<MTLComputeCommandEncoder> compute_command_encoder;
    PyObject* py_cbv_list;
    PyObject* py_srv_list;
    PyObject* py_uav_list;
} metal_Compute;

typedef struct metal_Swapchain
{
    PyObject_HEAD;
    metal_Device* py_device;
} metal_Swapchain;

typedef struct metal_MTLFunction
{
    PyObject_HEAD;
    id<MTLFunction> function;
} metal_MTLFunction;

static void metal_MTLFunction_dealloc(metal_MTLFunction* self)
{
    if (self->function)
    {
        [self->function release];
    }
    
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject metal_MTLFunction_Type = {
    PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.metal.MTLFunction", /* tp_name */
    sizeof(metal_MTLFunction),                                              /* tp_basicsize */
    0,                                                                      /* tp_itemsize */
    (destructor)metal_MTLFunction_dealloc,                                  /* tp_dealloc */
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
    "compushady metal MTLFunction",                                          /* tp_doc */
};

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
    
    if (self->command_queue)
        [self->command_queue release];
    
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

static void metal_Compute_dealloc(metal_Compute* self)
{
    if (self->compute_command_encoder)
        [self->compute_command_encoder release];
    
    if (self->compute_command_buffer)
        [self->compute_command_buffer release];
    
    if (self->py_device)
    {
        Py_DECREF(self->py_device);
    }
    
    Py_XDECREF(self->py_cbv_list);
    Py_XDECREF(self->py_srv_list);
    Py_XDECREF(self->py_uav_list);
    
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject metal_Compute_Type = {
    PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.metal.Compute", /* tp_name */
    sizeof(metal_Compute),                                                 /* tp_basicsize */
    0,                                                                     /* tp_itemsize */
    (destructor)metal_Compute_dealloc,                                     /* tp_dealloc */
    0,                                                                     /* tp_print */
    0,                                                                     /* tp_getattr */
    0,                                                                     /* tp_setattr */
    0,                                                                     /* tp_reserved */
    0,                                                                     /* tp_repr */
    0,                                                                     /* tp_as_number */
    0,                                                                     /* tp_as_sequence */
    0,                                                                     /* tp_as_mapping */
    0,                                                                     /* tp_hash  */
    0,                                                                     /* tp_call */
    0,                                                                     /* tp_str */
    0,                                                                     /* tp_getattro */
    0,                                                                     /* tp_setattro */
    0,                                                                     /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                                                     /* tp_flags */
    "compushady metal Compute",                                         /* tp_doc */
};

static void metal_Swapchain_dealloc(metal_Swapchain* self)
{
    if (self->py_device)
    {
        Py_DECREF(self->py_device);
    }
    
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject metal_Swapchain_Type = {
    PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.metal.Swapchain", /* tp_name */
    sizeof(metal_Swapchain),                                               /* tp_basicsize */
    0,                                                                       /* tp_itemsize */
    (destructor)metal_Swapchain_dealloc,                                   /* tp_dealloc */
    0,                                                                       /* tp_print */
    0,                                                                       /* tp_getattr */
    0,                                                                       /* tp_setattr */
    0,                                                                       /* tp_reserved */
    0,                                                                       /* tp_repr */
    0,                                                                       /* tp_as_number */
    0,                                                                       /* tp_as_sequence */
    0,                                                                       /* tp_as_mapping */
    0,                                                                       /* tp_hash  */
    0,                                                                       /* tp_call */
    0,                                                                       /* tp_str */
    0,                                                                       /* tp_getattro */
    0,                                                                       /* tp_setattro */
    0,                                                                       /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                                                       /* tp_flags */
    "compushady metal Swapchain",                                           /* tp_doc */
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
    
    [device release];
    
    return PyLong_FromUnsignedLongLong((unsigned long long)metal_layer);
}

static PyMemberDef metal_Resource_members[] = {
    {"size", T_ULONGLONG, offsetof(metal_Resource, size), 0, "resource size"},
    /*{"width", T_UINT, offsetof(vulkan_Resource, image_extent) + offsetof(VkExtent3D, width), 0, "resource width"},
     {"height", T_UINT, offsetof(vulkan_Resource, image_extent) + offsetof(VkExtent3D, height), 0, "resource height"},
     {"depth", T_UINT, offsetof(vulkan_Resource, image_extent) + offsetof(VkExtent3D, depth), 0, "resource depth"},
     {"row_pitch", T_UINT, offsetof(vulkan_Resource, row_pitch), 0, "resource row pitch"},*/
    {NULL} /* Sentinel */
};

static PyMemberDef metal_Swapchain_members[] = {
    /*{"width", T_UINT, offsetof(vulkan_Swapchain, image_extent) + offsetof(VkExtent2D, width), 0, "swapchain width"},
     {"height", T_UINT, offsetof(vulkan_Swapchain, image_extent) + offsetof(VkExtent2D, height), 0, "swapchain height"},*/
    {NULL} /* Sentinel */
};

static PyMethodDef metal_Compute_methods[] = {
    // {"dispatch", (PyCFunction)metal_Compute_dispatch, METH_VARARGS, "Execute a Compute Pipeline"},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

static PyMethodDef metal_Swapchain_methods[] = {
    //{"present", (PyCFunction)vulkan_Swapchain_present, METH_VARARGS, "Blit a texture resource to the Swapchain and present it"},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

static metal_Device* metal_Device_get_device(metal_Device* self)
{
    if (!self->command_queue)
    {
        self->command_queue = [self->device newCommandQueue];
    }
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

static PyObject* metal_Device_create_compute(metal_Device* self, PyObject* args, PyObject* kwds)
{
    const char* kwlist[] = { "shader", "cbv", "srv", "uav", NULL };
    PyObject* py_msl;
    PyObject* py_cbv = NULL;
    PyObject* py_srv = NULL;
    PyObject* py_uav = NULL;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*|OOO", (char**)kwlist,
                                     &py_msl, &py_cbv, &py_srv, &py_uav))
        return NULL;
    
    metal_Device* py_device = metal_Device_get_device(self);
    if (!py_device)
        return NULL;
    
    std::vector<metal_Resource*> cbv;
    std::vector<metal_Resource*> srv;
    std::vector<metal_Resource*> uav;
    
    if (!compushady_check_descriptors(&metal_Resource_Type, py_cbv, cbv, py_srv, srv, py_uav, uav))
    {
        return NULL;
    }
    
    metal_Compute* py_compute = (metal_Compute*)PyObject_New(metal_Compute, &metal_Compute_Type);
    if (!py_compute)
    {
        return PyErr_Format(PyExc_MemoryError, "unable to allocate metal Compute");
    }
    COMPUSHADY_CLEAR(py_compute);
    py_compute->py_device = py_device;
    Py_INCREF(py_compute->py_device);
    
    py_compute->py_cbv_list = PyList_New(0);
    py_compute->py_srv_list = PyList_New(0);
    py_compute->py_uav_list = PyList_New(0);
    
    py_compute->compute_command_buffer = [py_compute->py_device->command_queue commandBuffer];
    py_compute->compute_command_encoder = [py_compute->compute_command_buffer computeCommandEncoder];
    
    for (size_t i = 0; i<cbv.size();i++)
    {
        metal_Resource* py_resource = cbv[i];
        PyList_Append(py_compute->py_cbv_list, (PyObject*)py_resource);
        [py_compute->compute_command_encoder setBuffer:py_resource->buffer offset:0 atIndex:i];
    }
    
    for (size_t i = 0; i<srv.size();i++)
    {
        metal_Resource* py_resource = srv[i];
        PyList_Append(py_compute->py_srv_list, (PyObject*)py_resource);
    }
    
    for (size_t i = 0; i<uav.size();i++)
    {
        metal_Resource* py_resource = uav[i];
        PyList_Append(py_compute->py_uav_list, (PyObject*)py_resource);
    }
    
    
    
    return (PyObject*)py_compute;
}


static PyObject* metal_Device_get_debug_messages(metal_Device * self, PyObject * args)
{
    PyObject* py_list = PyList_New(0);
    
    return py_list;
}


static PyMethodDef metal_Device_methods[] = {
    {"create_buffer", (PyCFunction)metal_Device_create_buffer, METH_VARARGS, "Creates a Buffer object"},
    {"create_compute", (PyCFunction)metal_Device_create_compute, METH_VARARGS | METH_KEYWORDS, "Creates a Compute object"},
    {"get_debug_messages", (PyCFunction)metal_Device_get_debug_messages, METH_VARARGS, "Get Device's debug messages"},
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

static PyObject* metal_Resource_readback_to_buffer(metal_Resource* self, PyObject* args)
{
    Py_buffer view;
    size_t offset = 0;
    if (!PyArg_ParseTuple(args, "y*K", &view, &offset))
        return NULL;
    
    if (offset > self->size)
    {
        PyBuffer_Release(&view);
        return PyErr_Format(PyExc_ValueError, "requested buffer out of bounds: %llu (expected no more than %llu)", offset, self->size);
    }
    
    char* mapped_data = (char*)[self->buffer contents];
    
    memcpy(view.buf, mapped_data + offset, Py_MIN((size_t)view.len, self->size - offset));
    
    PyBuffer_Release(&view);
    Py_RETURN_NONE;
}


static PyObject* metal_Resource_copy_to(metal_Resource * self, PyObject * args)
{
    PyObject* py_destination;
    if (!PyArg_ParseTuple(args, "O", &py_destination))
        return NULL;
    
    int ret = PyObject_IsInstance(py_destination, (PyObject*)&metal_Resource_Type);
    if (ret < 0)
    {
        return NULL;
    }
    else if (ret == 0)
    {
        return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
    }
    
    metal_Resource* dst_resource = (metal_Resource*)py_destination;
    size_t dst_size = ((metal_Resource*)py_destination)->size;
    
    if (self->size > dst_size)
    {
        return PyErr_Format(PyExc_ValueError, "Resource size is bigger than destination size: %llu (expected no more than %llu)", self->size, dst_size);
    }
    
    id<MTLCommandBuffer> blit_command_buffer = [self->py_device->command_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit_command_encoder = [blit_command_buffer blitCommandEncoder];
    
    if (self->buffer && dst_resource->buffer)
    {
        [blit_command_encoder copyFromBuffer:self->buffer sourceOffset:0 toBuffer:dst_resource->buffer destinationOffset:0 size:self->size];
    }
    else if (self->buffer) // buffer to image
    {
        
    }
    else if (dst_resource->buffer) // image to buffer
    {
        
    }
    else // image to image
    {
        
    }
    
    [blit_command_encoder endEncoding];
    
    [blit_command_buffer commit];
    [blit_command_buffer waitUntilCompleted];
    
    [blit_command_encoder release];
    [blit_command_buffer release];
    
    Py_RETURN_NONE;
}



static PyMethodDef metal_Resource_methods[] = {
    {"upload", (PyCFunction)metal_Resource_upload, METH_VARARGS, "Upload bytes to a GPU Resource"},
    {"readback", (PyCFunction)metal_Resource_readback, METH_VARARGS, "Readback bytes from a GPU Resource"},
    {"readback_to_buffer", (PyCFunction)metal_Resource_readback_to_buffer, METH_VARARGS, "Readback into a buffer from a GPU Resource"},
    {"copy_to", (PyCFunction)metal_Resource_copy_to, METH_VARARGS, "Copy resource content to another resource"},
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

static PyObject* metal_enable_debug(PyObject * self)
{
    Py_RETURN_NONE;
}

static PyObject* metal_get_shader_binary_type(PyObject * self)
{
    return PyLong_FromLong(COMPUSHADY_SHADER_BINARY_TYPE_MSL);
}

static PyObject* compushady_msl_compile(PyObject* self, PyObject* args)
{
    Py_buffer view;
    PyObject* py_entry_point;
    if (!PyArg_ParseTuple(args, "s*U", &view, &py_entry_point))
        return NULL;
    
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    
    NSString* source = [[NSString alloc] initWithBytes:view.buf length:view.len encoding:NSUTF8StringEncoding];
    
    id<MTLLibrary> library = [device newLibraryWithSource:source options:NULL error:NULL];
    
    const char* function_name_utf8 = PyUnicode_AsUTF8AndSize(py_entry_point, NULL);
    
    NSString* function_name = [[NSString alloc] initWithUTF8String:function_name_utf8];
    
    id<MTLFunction> function = [library newFunctionWithName:function_name];
    if (!function)
    {
        return PyErr_Format(PyExc_Exception, "unable to find function %s in MTLLibrary",function_name_utf8);
    }
    
    metal_MTLFunction* py_mtl_function = (metal_MTLFunction*)PyObject_New(metal_MTLFunction, &metal_MTLFunction_Type);
    if (!py_mtl_function)
    {
        return PyErr_Format(PyExc_MemoryError, "unable to allocate metal MTLFunction");
    }
    COMPUSHADY_CLEAR(py_mtl_function);
    py_mtl_function->function = function;
    
    [function_name release];
    
    [source release];
    
    [library release];
    
    [device release];
    
    return (PyObject*)py_mtl_function;
}

static PyMethodDef compushady_backends_metal_methods[] = {
    {"create_metal_layer", (PyCFunction)compushady_create_metal_layer, METH_VARARGS, "Creates a CAMetalLayer on the default device"},
    {"msl_compile", (PyCFunction)compushady_msl_compile, METH_VARARGS, "Compile a MSL shader"},
    {"enable_debug", (PyCFunction)metal_enable_debug, METH_NOARGS, "Enable GPU debug mode"},
    {"get_shader_binary_type", (PyCFunction)metal_get_shader_binary_type, METH_NOARGS, "Returns the required shader binary type"},
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
    PyObject* m = compushady_backend_init(
                                          &compushady_backends_metal_module,
                                          &metal_Device_Type, metal_Device_members, metal_Device_methods,
                                          &metal_Resource_Type, metal_Resource_members, metal_Resource_methods,
                                          &metal_Swapchain_Type, metal_Swapchain_members, metal_Swapchain_methods,
                                          &metal_Compute_Type, NULL, metal_Compute_methods
                                          );
    
    if (!m)
        return NULL;
    
    if (PyType_Ready(&metal_MTLFunction_Type) < 0)
    {
        Py_DECREF(&metal_Swapchain_Type);
        Py_DECREF(&metal_Compute_Type);
        Py_DECREF(&metal_Resource_Type);
        Py_DECREF(&metal_Device_Type);
        Py_DECREF(m);
        return NULL;
    }
    Py_INCREF(&metal_MTLFunction_Type);
    if (PyModule_AddObject(m, "MTLFunction", (PyObject*)&metal_MTLFunction_Type) < 0)
    {
        Py_DECREF(&metal_MTLFunction_Type);
        Py_DECREF(&metal_Swapchain_Type);
        Py_DECREF(&metal_Compute_Type);
        Py_DECREF(&metal_Resource_Type);
        Py_DECREF(&metal_Device_Type);
        Py_DECREF(m);
        return NULL;
    }
    
    return m;
}
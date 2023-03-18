#include "compushady.h"
#import <MetalKit/MetalKit.h>

std::unordered_map<uint32_t, std::pair<MTLPixelFormat, uint32_t> > metal_formats;

#define MTL_FORMAT(x, y, size) metal_formats[x] = std::pair<MTLPixelFormat, uint32_t>(y, size)

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
    id<MTLTexture> texture;
    size_t size;
    uint32_t stride;
    uint32_t row_pitch;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} metal_Resource;

typedef struct metal_Sampler
{
    PyObject_HEAD;
    metal_Device* py_device;
    id<MTLSamplerState> sampler;
} metal_Sampler;

typedef struct metal_MTLFunction
{
    PyObject_HEAD;
    id<MTLFunction> function;
    uint32_t x;
    uint32_t y;
    uint32_t z;
} metal_MTLFunction;

typedef struct metal_Compute
{
    PyObject_HEAD;
    metal_Device* py_device;
    id<MTLComputePipelineState> compute_pipeline_state;
    PyObject* py_cbv_list;
    PyObject* py_srv_list;
    PyObject* py_uav_list;
    PyObject* py_samplers_list;
    std::vector<metal_Resource*> cbv;
    std::vector<metal_Resource*> srv;
    std::vector<metal_Resource*> uav;
    std::vector<metal_Sampler*> samplers;
    metal_MTLFunction* py_mtl_function;
} metal_Compute;

typedef struct metal_Swapchain
{
    PyObject_HEAD;
    metal_Device* py_device;
    CAMetalLayer* metal_layer;
} metal_Swapchain;

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
    
    if (self->texture)
    {
        [self->texture release];
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

static void metal_Sampler_dealloc(metal_Sampler* self)
{
    if (self->sampler)
    {
        [self->sampler release];
    }
    
    Py_XDECREF(self->py_device);
    
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject metal_Sampler_Type = {
    PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.metal.Sampler", /* tp_name */
    sizeof(metal_Sampler),                                              /* tp_basicsize */
    0,                                                                      /* tp_itemsize */
    (destructor)metal_Sampler_dealloc,                                  /* tp_dealloc */
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
    "compushady metal Sampler",                                          /* tp_doc */
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
    if (self->compute_pipeline_state)
        [self->compute_pipeline_state release];
    
    Py_XDECREF(self->py_device);
    
    Py_XDECREF(self->py_cbv_list);
    Py_XDECREF(self->py_srv_list);
    Py_XDECREF(self->py_uav_list);

     Py_XDECREF(self->py_samplers_list);
    
    Py_XDECREF(self->py_mtl_function);
    
    self->cbv = std::vector<metal_Resource*>();
    self->srv = std::vector<metal_Resource*>();
    self->uav = std::vector<metal_Resource*>();
    
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

static PyObject* compushady_create_metal_layer(metal_Device* self, PyObject* args)
{
    NSWindow* window_handle;
    int format;
    if (!PyArg_ParseTuple(args, "Ki", &window_handle, &format))
        return NULL;

    if (metal_formats.find(format) == metal_formats.end())
    {
        return PyErr_Format(PyExc_ValueError, "invalid pixel format");
    }
    
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    CAMetalLayer* metal_layer = [CAMetalLayer layer];
    metal_layer.device = device;
    metal_layer.pixelFormat = metal_formats[format].first;
    metal_layer.framebufferOnly = YES;
    metal_layer.frame = [window_handle.contentView frame];
    window_handle.contentView.layer = metal_layer;

    [device release];
    
    return PyLong_FromUnsignedLongLong((unsigned long long)metal_layer);
}

static PyMemberDef metal_Resource_members[] = {
    {"size", T_ULONGLONG, offsetof(metal_Resource, size), 0, "resource size"},
    {"width", T_UINT, offsetof(metal_Resource, width), 0, "resource width"},
    {"height", T_UINT, offsetof(metal_Resource, height), 0, "resource height"},
    {"depth", T_UINT, offsetof(metal_Resource, depth), 0, "resource depth"},
    {"row_pitch", T_UINT, offsetof(metal_Resource, row_pitch), 0, "resource row pitch"},
    {NULL} /* Sentinel */
};

static PyMemberDef metal_Swapchain_members[] = {
    /*{"width", T_UINT, offsetof(vulkan_Swapchain, image_extent) + offsetof(VkExtent2D, width), 0, "swapchain width"},
     {"height", T_UINT, offsetof(vulkan_Swapchain, image_extent) + offsetof(VkExtent2D, height), 0, "swapchain height"},*/
    {NULL} /* Sentinel */
};

static PyObject* metal_Compute_dispatch(metal_Compute * self, PyObject * args)
{
    uint32_t x, y, z;
    if (!PyArg_ParseTuple(args, "III", &x, &y, &z))
        return NULL;
    
    id<MTLCommandBuffer> compute_command_buffer = [self->py_device->command_queue commandBuffer];
    id<MTLComputeCommandEncoder> compute_command_encoder = [compute_command_buffer computeCommandEncoder];
    
    [compute_command_encoder setComputePipelineState:self->compute_pipeline_state];

    uint32_t buffer_index = 0;
    uint32_t texture_index = 0;
    uint32_t sampler_index = 0;

    for(size_t i = 0; i < self->cbv.size(); i++)
    {
        metal_Resource* py_resource = self->cbv[i];
        if (py_resource->texture)
        	[compute_command_encoder setTexture:py_resource->texture atIndex:texture_index++];
	else
        	[compute_command_encoder setBuffer:py_resource->buffer offset:0 atIndex:buffer_index++];
    }

    for(size_t i = 0; i < self->srv.size(); i++)
    {
        metal_Resource* py_resource = self->srv[i];
        if (py_resource->texture)
        	[compute_command_encoder setTexture:py_resource->texture atIndex:texture_index++];
	else
        	[compute_command_encoder setBuffer:py_resource->buffer offset:0 atIndex:buffer_index++];
    }
    
    for(size_t i = 0; i < self->uav.size(); i++)
    {
        metal_Resource* py_resource = self->uav[i];
        if (py_resource->texture)
        	[compute_command_encoder setTexture:py_resource->texture atIndex:texture_index++];
	else
        	[compute_command_encoder setBuffer:py_resource->buffer offset:0 atIndex:buffer_index++];
    }

     for(size_t i = 0; i < self->samplers.size(); i++)
    {
        metal_Sampler* py_sampler = self->samplers[i];
        [compute_command_encoder setSamplerState:py_sampler->sampler atIndex:sampler_index++];
    }

    [compute_command_encoder dispatchThreadgroups:MTLSizeMake(x, y, z) threadsPerThreadgroup:MTLSizeMake(self->py_mtl_function->x, self->py_mtl_function->y, self->py_mtl_function->z)];
    
    [compute_command_encoder endEncoding];

    [compute_command_buffer commit];
    [compute_command_buffer waitUntilCompleted];
    
    [compute_command_encoder release];
    [compute_command_buffer release];
    
    Py_RETURN_NONE;
}

static PyMethodDef metal_Compute_methods[] = {
    {"dispatch", (PyCFunction)metal_Compute_dispatch, METH_VARARGS, "Execute a Compute Pipeline"},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* metal_Swapchain_present(metal_Swapchain* self, PyObject* args)
{
        PyObject* py_resource;
        uint32_t x;
        uint32_t y;
        if (!PyArg_ParseTuple(args, "OII", &py_resource, &x, &y))
                return NULL;

        int ret = PyObject_IsInstance(py_resource, (PyObject*)&metal_Resource_Type);
        if (ret < 0)
        {
                return NULL;
        }
        else if (ret == 0)
        {
                return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
        }
        metal_Resource* src_resource = (metal_Resource*)py_resource;
        if (!src_resource->texture)
        {
                return PyErr_Format(PyExc_ValueError, "Expected a Texture object");
        }

	id<CAMetalDrawable> drawable = [self->metal_layer nextDrawable];

	id<MTLTexture> texture = drawable.texture;

        //x = Py_MIN(x, self->desc.Width - 1);
        //y = Py_MIN(y, self->desc.Height - 1);

	id<MTLCommandBuffer> blit_command_buffer = [self->py_device->command_queue commandBuffer];
	id<MTLBlitCommandEncoder> blit_command_encoder = [blit_command_buffer blitCommandEncoder];

        [blit_command_encoder copyFromTexture:src_resource->texture toTexture:texture];

    	[blit_command_encoder endEncoding];

        [blit_command_buffer presentDrawable:drawable];

    	[blit_command_buffer commit];
    	[blit_command_buffer waitUntilCompleted];

    	[blit_command_encoder release];
    	[blit_command_buffer release];

	[drawable release];

        Py_RETURN_NONE;
}

static PyMethodDef metal_Swapchain_methods[] = {
    {"present", (PyCFunction)metal_Swapchain_present, METH_VARARGS, "Blit a texture resource to the Swapchain and present it"},
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
        return PyErr_Format(Compushady_BufferError, "zero size buffer");

    if (format > 0)
    {
        if (metal_formats.find(format) == metal_formats.end())
        {
            return PyErr_Format(PyExc_ValueError, "invalid pixel format");
        }
    }
    
    metal_Device* py_device = metal_Device_get_device(self);
    if (!py_device)
        return NULL;
    
    MTLResourceOptions options = MTLResourceStorageModePrivate;
    MTLStorageMode storage_mode = MTLStorageModePrivate;
    
    switch (heap)
    {
        case COMPUSHADY_HEAP_DEFAULT:
            break;
        case COMPUSHADY_HEAP_UPLOAD:
            options = MTLResourceStorageModeShared;
            storage_mode = MTLStorageModeShared;
            break;
        case COMPUSHADY_HEAP_READBACK:
            options = MTLResourceStorageModeShared;
            storage_mode = MTLStorageModeShared;
            break;
        default:
            return PyErr_Format(PyExc_Exception, "Invalid heap type: %d", heap);
    }
    
    id<MTLBuffer> buffer = [py_device->device newBufferWithLength:size options:options];
    if (!buffer)
    {
        return PyErr_Format(Compushady_BufferError, "unable to create metal Buffer");
    }
    
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

    if (format > 0)
    {
        MTLTextureDescriptor *texture_descriptor = [MTLTextureDescriptor new];
        texture_descriptor.textureType = MTLTextureType2D;
        texture_descriptor.pixelFormat = metal_formats[format].first;
        texture_descriptor.arrayLength = 1;
        texture_descriptor.mipmapLevelCount = 1;
        texture_descriptor.width = size / metal_formats[format].second;
        texture_descriptor.height = 1;
        texture_descriptor.depth = 1;
        texture_descriptor.sampleCount = 1;
        texture_descriptor.storageMode = storage_mode;
        texture_descriptor.swizzle = MTLTextureSwizzleChannelsDefault;
        texture_descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    
        py_resource->texture = [py_resource->buffer newTextureWithDescriptor:texture_descriptor offset:0 bytesPerRow:COMPUSHADY_ALIGN(size, [py_device->device minimumTextureBufferAlignmentForPixelFormat:texture_descriptor.pixelFormat])];
        py_resource->row_pitch = size;
        py_resource->width = texture_descriptor.width;
        py_resource->height = 1;
        py_resource->depth = 1;
    
        [texture_descriptor release];
    }
 
    return (PyObject*)py_resource;
}

static PyObject* metal_Device_create_compute(metal_Device* self, PyObject* args, PyObject* kwds)
{
    const char* kwlist[] = { "shader", "cbv", "srv", "uav", "samplers", NULL };
    PyObject* py_msl;
    PyObject* py_cbv = NULL;
    PyObject* py_srv = NULL;
    PyObject* py_uav = NULL;
    PyObject* py_samplers = NULL;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOOO", (char**)kwlist,
                                     &py_msl, &py_cbv, &py_srv, &py_uav, &py_samplers))
        return NULL;
    
    int ret = PyObject_IsInstance(py_msl, (PyObject*)&metal_MTLFunction_Type);
    if (ret < 0)
    {
        return NULL;
    }
    else if (ret == 0)
    {
        return PyErr_Format(PyExc_ValueError, "Expected a MTLFunction object");
    }
    
    metal_MTLFunction* mtl_function = (metal_MTLFunction*)py_msl;

    metal_Device* py_device = metal_Device_get_device(self);
    if (!py_device)
        return NULL;
    
    metal_Compute* py_compute = (metal_Compute*)PyObject_New(metal_Compute, &metal_Compute_Type);
    if (!py_compute)
    {
        return PyErr_Format(PyExc_MemoryError, "unable to allocate metal Compute");
    }
    COMPUSHADY_CLEAR(py_compute);
    py_compute->py_device = py_device;
    Py_INCREF(py_compute->py_device);
    
    py_compute->cbv = std::vector<metal_Resource*>();
    py_compute->srv = std::vector<metal_Resource*>();
    py_compute->uav = std::vector<metal_Resource*>();
    py_compute->samplers = std::vector<metal_Sampler*>();
    
    py_compute->py_mtl_function = mtl_function;
    Py_INCREF(py_compute->py_mtl_function);
    
    if (!compushady_check_descriptors(&metal_Resource_Type, py_cbv, py_compute->cbv, py_srv, py_compute->srv, py_uav, py_compute->uav, &metal_Sampler_Type, py_samplers, py_compute->samplers))
    {
        Py_DECREF(py_compute);
        return NULL;
    }
    
    py_compute->py_cbv_list = PyList_New(0);
    py_compute->py_srv_list = PyList_New(0);
    py_compute->py_uav_list = PyList_New(0);
    py_compute->py_samplers_list = PyList_New(0);
    
    py_compute->compute_pipeline_state = [py_device->device newComputePipelineStateWithFunction:mtl_function->function error:nil];
    if (!py_compute->compute_pipeline_state)
    {
        Py_DECREF(py_compute);
        return PyErr_Format(PyExc_Exception, "unable to create metal ComputePipelineState");
    }
    
    for (size_t i = 0; i<py_compute->cbv.size();i++)
    {
        metal_Resource* py_resource = py_compute->cbv[i];
        PyList_Append(py_compute->py_cbv_list, (PyObject*)py_resource);
    }
    
    for (size_t i = 0; i<py_compute->srv.size();i++)
    {
        metal_Resource* py_resource = py_compute->srv[i];
        PyList_Append(py_compute->py_srv_list, (PyObject*)py_resource);
    }
    
    for (size_t i = 0; i<py_compute->uav.size();i++)
    {
        metal_Resource* py_resource = py_compute->uav[i];
        PyList_Append(py_compute->py_uav_list, (PyObject*)py_resource);
    }

    for (size_t i = 0; i<py_compute->samplers.size();i++)
    {
        metal_Sampler* py_sampler = py_compute->samplers[i];
        PyList_Append(py_compute->py_samplers_list, (PyObject*)py_sampler);
    }

    return (PyObject*)py_compute;
}


static PyObject* metal_Device_get_debug_messages(metal_Device * self, PyObject * args)
{
    PyObject* py_list = PyList_New(0);
    
    return py_list;
}

static PyObject* metal_Device_create_texture2d(metal_Device* self, PyObject* args)
{
    uint32_t width;
    uint32_t height;
    int format;
    if (!PyArg_ParseTuple(args, "IIi", &width, &height, &format))
        return NULL;

    if (metal_formats.find(format) == metal_formats.end())
    {
        return PyErr_Format(PyExc_ValueError, "invalid pixel format");
    }
    
    metal_Device* py_device = metal_Device_get_device(self);
    if (!py_device)
        return NULL;
    
    metal_Resource* py_resource = (metal_Resource*)PyObject_New(metal_Resource, &metal_Resource_Type);
    if (!py_resource)
    {
        return PyErr_Format(PyExc_MemoryError, "unable to allocate metal Resource");
    }
    COMPUSHADY_CLEAR(py_resource);
    py_resource->py_device = py_device;
    Py_INCREF(py_resource->py_device);
    
    MTLTextureDescriptor *texture_descriptor = [MTLTextureDescriptor new];
    texture_descriptor.textureType = MTLTextureType2D;
    texture_descriptor.pixelFormat = metal_formats[format].first;
    texture_descriptor.arrayLength = 1;
    texture_descriptor.mipmapLevelCount = 1;
    texture_descriptor.width = width;
    texture_descriptor.height = height;
    texture_descriptor.depth = 1;
    texture_descriptor.storageMode = MTLStorageModePrivate;
    texture_descriptor.resourceOptions = MTLResourceStorageModePrivate;
    texture_descriptor.sampleCount = 1;
    texture_descriptor.swizzle = MTLTextureSwizzleChannelsDefault;
    texture_descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    
    py_resource->texture = [py_device->device newTextureWithDescriptor:texture_descriptor];
    if (!py_resource->texture)
    {
        [texture_descriptor release];
	Py_DECREF(py_resource);
        return PyErr_Format(PyExc_Exception, "unable to create metal Texture");
    }
    py_resource->row_pitch = width * metal_formats[format].second;
    py_resource->width = width;
    py_resource->height = height;
    py_resource->depth = 1;
    py_resource->size = py_resource->row_pitch * height;
    
    [texture_descriptor release];
    
    return (PyObject*)py_resource;
}

static PyObject* metal_Device_create_texture1d(metal_Device* self, PyObject* args)
{
    uint32_t width;
    int format;
    if (!PyArg_ParseTuple(args, "Ii", &width, &format))
        return NULL;
    
    metal_Device* py_device = metal_Device_get_device(self);
    if (!py_device)
        return NULL;
    
    metal_Resource* py_resource = (metal_Resource*)PyObject_New(metal_Resource, &metal_Resource_Type);
    if (!py_resource)
    {
        return PyErr_Format(PyExc_MemoryError, "unable to allocate metal Resource");
    }
    COMPUSHADY_CLEAR(py_resource);
    py_resource->py_device = py_device;
    Py_INCREF(py_resource->py_device);
    
    MTLTextureDescriptor *texture_descriptor = [MTLTextureDescriptor new];
    texture_descriptor.textureType = MTLTextureType1D;
    texture_descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    texture_descriptor.arrayLength = 1;
    texture_descriptor.mipmapLevelCount = 1;
    texture_descriptor.width = width;
    texture_descriptor.height = 1;
    texture_descriptor.depth = 1;
    texture_descriptor.resourceOptions = MTLResourceStorageModePrivate;
    texture_descriptor.sampleCount = 1;
    texture_descriptor.swizzle = MTLTextureSwizzleChannelsDefault;
    texture_descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    
    py_resource->texture = [py_device->device newTextureWithDescriptor:texture_descriptor];
    py_resource->row_pitch = width * 4;
    py_resource->width = width;
    py_resource->height = 1;
    py_resource->depth = 1;
    py_resource->size = width * 4;
    
    [texture_descriptor release];
    
    return (PyObject*)py_resource;
}

static PyObject* metal_Device_create_texture3d(metal_Device* self, PyObject* args)
{
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    int format;
    if (!PyArg_ParseTuple(args, "IIIi", &width, &height, &depth, &format))
        return NULL;
    
    metal_Device* py_device = metal_Device_get_device(self);
    if (!py_device)
        return NULL;
    
    metal_Resource* py_resource = (metal_Resource*)PyObject_New(metal_Resource, &metal_Resource_Type);
    if (!py_resource)
    {
        return PyErr_Format(PyExc_MemoryError, "unable to allocate metal Resource");
    }
    COMPUSHADY_CLEAR(py_resource);
    py_resource->py_device = py_device;
    Py_INCREF(py_resource->py_device);
    
    MTLTextureDescriptor *texture_descriptor = [MTLTextureDescriptor new];
    texture_descriptor.textureType = MTLTextureType3D;
    texture_descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    texture_descriptor.arrayLength = 1;
    texture_descriptor.mipmapLevelCount = 1;
    texture_descriptor.width = width;
    texture_descriptor.height = height;
    texture_descriptor.depth = depth;
    texture_descriptor.resourceOptions = MTLResourceStorageModePrivate;
    texture_descriptor.sampleCount = 1;
    texture_descriptor.swizzle = MTLTextureSwizzleChannelsDefault;
    texture_descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    
    py_resource->texture = [py_device->device newTextureWithDescriptor:texture_descriptor];
    py_resource->row_pitch = width * 4;
    py_resource->width = width;
    py_resource->height = height;
    py_resource->depth = depth;
    py_resource->size = width * 4 * height * depth;
    
    [texture_descriptor release];
    
    return (PyObject*)py_resource;
}

static PyObject* metal_Device_create_swapchain(metal_Device* self, PyObject* args)
{
        CAMetalLayer* metal_layer;
        int format;
        uint32_t num_buffers;
        int width = -1;
        int height = -1;
        if (!PyArg_ParseTuple(args, "KiIii", &metal_layer, &format, &num_buffers, &width, &height))
                return NULL;

	if (metal_formats.find(format) == metal_formats.end())
    	{
        	return PyErr_Format(PyExc_ValueError, "invalid pixel format");
    	}

        metal_Device* py_device = metal_Device_get_device(self);
        if (!py_device)
                return NULL;

        metal_Swapchain* py_swapchain = (metal_Swapchain*)PyObject_New(metal_Swapchain, &metal_Swapchain_Type);
        if (!py_swapchain)
        {
                return PyErr_Format(PyExc_MemoryError, "unable to allocate metal Swapchain");
        }
        COMPUSHADY_CLEAR(py_swapchain);
        py_swapchain->py_device = py_device;
        Py_INCREF(py_swapchain->py_device);

	metal_layer.pixelFormat = metal_formats[format].first;
	metal_layer.device = self->device;
	py_swapchain->metal_layer = metal_layer;

        return (PyObject*)py_swapchain;
}

#define COMPUSHADY_METAL_SAMPLER_ADDRESS_MODE(var, field) if (var == COMPUSHADY_SAMPLER_ADDRESS_MODE_WRAP)\
{\
	var = MTLSamplerAddressModeRepeat;\
}\
else if (var == COMPUSHADY_SAMPLER_ADDRESS_MODE_MIRROR)\
{\
	var = MTLSamplerAddressModeMirrorRepeat;\
}\
else if (var == COMPUSHADY_SAMPLER_ADDRESS_MODE_CLAMP)\
{\
	var = MTLSamplerAddressModeMirrorClampToEdge;\
}\
else\
{\
	return PyErr_Format(Compushady_SamplerError, "unsupported address mode for " field);\
}

static PyObject* metal_Device_create_sampler(metal_Device* self, PyObject* args)
{
    int address_mode_u;
	int address_mode_v;
	int address_mode_w;
	int filter_min;
	int filter_mag;
	if (!PyArg_ParseTuple(args, "iiiii", &address_mode_u, &address_mode_v, &address_mode_w, &filter_min, &filter_mag))
		return NULL;

	COMPUSHADY_METAL_SAMPLER_ADDRESS_MODE(address_mode_u, "U");
	COMPUSHADY_METAL_SAMPLER_ADDRESS_MODE(address_mode_v, "V");
	COMPUSHADY_METAL_SAMPLER_ADDRESS_MODE(address_mode_w, "W");

    if (filter_min == COMPUSHADY_SAMPLER_FILTER_POINT)
	{
		filter_min = MTLSamplerMinMagFilterNearest;
	}
	else if (filter_min == COMPUSHADY_SAMPLER_FILTER_LINEAR)
	{
		filter_min = MTLSamplerMinMagFilterLinear;
	}
    else
    {
        return PyErr_Format(Compushady_SamplerError, "unsupported min filter");
    }

    if (filter_mag == COMPUSHADY_SAMPLER_FILTER_POINT)
	{
		filter_mag = MTLSamplerMinMagFilterNearest;
	}
	else if (filter_mag == COMPUSHADY_SAMPLER_FILTER_LINEAR)
	{
		filter_mag = MTLSamplerMinMagFilterLinear;
	}
    else
    {
        return PyErr_Format(Compushady_SamplerError, "unsupported mag filter");
    }

    metal_Device* py_device = metal_Device_get_device(self);
    if (!py_device)
        return NULL;
    
    metal_Sampler* py_sampler = (metal_Sampler*)PyObject_New(metal_Sampler, &metal_Sampler_Type);
    if (!py_sampler)
    {
        return PyErr_Format(PyExc_MemoryError, "unable to allocate metal Sampler");
    }
    COMPUSHADY_CLEAR(py_sampler);
    py_sampler->py_device = py_device;
    Py_INCREF(py_sampler->py_device);
    
    MTLSamplerDescriptor *sampler_descriptor = [MTLSamplerDescriptor new];
    sampler_descriptor.normalizedCoordinates = true;
    sampler_descriptor.rAddressMode = address_mode_w;
    sampler_descriptor.sAddressMode = address_mode_u;
    sampler_descriptor.tAddressMode = address_mode_v;
    sampler_descriptor.minFilter = filter_min;
    sampler_descriptor.magFilter = filter_mag;
    
    py_sampler->sampler = [py_device->device newSamplerStateWithDescriptor:sampler_descriptor];
    
    [sampler_descriptor release];
    
    return (PyObject*)py_sampler;
}

static PyMethodDef metal_Device_methods[] = {
    {"create_buffer", (PyCFunction)metal_Device_create_buffer, METH_VARARGS, "Creates a Buffer object"},
    {"create_texture2d", (PyCFunction)metal_Device_create_texture2d, METH_VARARGS, "Creates a Texture2D object"},
    {"create_texture1d", (PyCFunction)metal_Device_create_texture1d, METH_VARARGS, "Creates a Texture1D object"},
    {"create_texture3d", (PyCFunction)metal_Device_create_texture3d, METH_VARARGS, "Creates a Texture3D object"},
    {"create_compute", (PyCFunction)metal_Device_create_compute, METH_VARARGS | METH_KEYWORDS, "Creates a Compute object"},
    {"get_debug_messages", (PyCFunction)metal_Device_get_debug_messages, METH_VARARGS, "Get Device's debug messages"},
    {"create_swapchain", (PyCFunction)metal_Device_create_swapchain, METH_VARARGS, "Creates a Swapchain object"},
    {"create_sampler", (PyCFunction)metal_Device_create_sampler, METH_VARARGS, "Creates a Sampler object"},
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

static PyObject* metal_Resource_upload2d(metal_Resource* self, PyObject* args)
{
    Py_buffer view;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_pixel;
    if (!PyArg_ParseTuple(args, "y*IIII", &view, &pitch, &width, &height, &bytes_per_pixel))
        return NULL;
    
    char* mapped_data = (char*)[self->buffer contents];
    
    size_t offset = 0;
    size_t remains = view.len;
    size_t resource_remains = self->size;
    for (uint32_t y = 0; y < height; y++)
    {
        size_t amount = Py_MIN(width * bytes_per_pixel, Py_MIN(remains, resource_remains));
        memcpy(mapped_data + (pitch * y), (char*)view.buf + offset, amount);
        remains -= amount;
        if (remains == 0)
            break;
        resource_remains -= amount;
        offset += amount;
    }
    
    PyBuffer_Release(&view);
    
    Py_RETURN_NONE;
}

static PyObject* metal_Resource_upload_chunked(metal_Resource* self, PyObject* args)
{
        Py_buffer view;
        uint32_t stride;
        Py_buffer filler;
        if (!PyArg_ParseTuple(args, "y*Iy*", &view, &stride, &filler))
                return NULL;

        size_t elements = view.len / stride;
        size_t additional_bytes = elements * filler.len;

        if (view.len + additional_bytes > self->size)
        {
                PyBuffer_Release(&view);
                PyBuffer_Release(&filler);
                return PyErr_Format(PyExc_ValueError, "supplied buffer is bigger than resource size: %llu (expected no more than %llu)", view.len + additional_bytes, self->size);
        }

        char* mapped_data = (char*)[self->buffer contents];

        size_t offset = 0;
        for (uint32_t i = 0; i < elements; i++)
        {
                memcpy(mapped_data + offset, (char*)view.buf + (i * stride), stride);
                offset += stride;
                memcpy(mapped_data + offset, (char*)filler.buf, filler.len);
                offset += filler.len;
        }

        PyBuffer_Release(&view);
        PyBuffer_Release(&filler);
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
        [blit_command_encoder copyFromBuffer:self->buffer sourceOffset:0 sourceBytesPerRow:dst_resource->row_pitch sourceBytesPerImage:dst_resource->row_pitch * dst_resource->height sourceSize:MTLSizeMake(dst_resource->width, dst_resource->height, dst_resource->depth) toTexture:dst_resource->texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
    }
    else if (dst_resource->buffer) // image to buffer
    {
        [blit_command_encoder copyFromTexture:self->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(self->width, self->height, self->depth) toBuffer:dst_resource->buffer destinationOffset:0 destinationBytesPerRow:self->row_pitch destinationBytesPerImage:self->row_pitch * self->height];
    }
    else // image to image
    {
        [blit_command_encoder copyFromTexture:self->texture toTexture:dst_resource->texture];
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
    {"upload2d", (PyCFunction)metal_Resource_upload2d, METH_VARARGS, "Upload bytes to a GPU Resource given pitch, width, height and pixel size"},
    {"upload_chunked", (PyCFunction)metal_Resource_upload_chunked, METH_VARARGS, "Upload bytes to a GPU Resource with the given stride and a filler"},
    {"readback", (PyCFunction)metal_Resource_readback, METH_VARARGS, "Readback bytes from a GPU Resource"},
    {"readback_to_buffer", (PyCFunction)metal_Resource_readback_to_buffer, METH_VARARGS, "Readback into a buffer from a GPU Resource"},
    {"copy_to", (PyCFunction)metal_Resource_copy_to, METH_VARARGS, "Copy resource content to another resource"},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* metal_get_discovered_devices(PyObject * self)
{
    PyObject* py_list = PyList_New(0);
    
    NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
    
    for (id<MTLDevice> device in devices)
    {
        metal_Device* py_device = (metal_Device*)PyObject_New(metal_Device, &metal_Device_Type);
        if (!py_device)
        {
            Py_DECREF(py_list);
            return PyErr_Format(PyExc_MemoryError, "unable to allocate metal Device");
        }
        COMPUSHADY_CLEAR(py_device);
        
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
    uint32_t x;
    uint32_t y;
    uint32_t z;
    if (!PyArg_ParseTuple(args, "s*U(III)", &view, &py_entry_point, &x, &y, &z))
        return NULL;
    
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    
    NSString* source = [[NSString alloc] initWithBytes:view.buf length:view.len encoding:NSUTF8StringEncoding];
    
    NSError *error = nil;

    id<MTLLibrary> library = [device newLibraryWithSource:source options:NULL error:&error];
    if (!library)
    {
        PyObject* py_exc = PyErr_Format(PyExc_Exception, "unable to compile shader: %s", [[error localizedDescription] UTF8String]);
        if (error)
            [error release];
        return py_exc;
    }
    
    if (error)
        [error release];
    
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

    py_mtl_function->x = x;
    py_mtl_function->y = y;
    py_mtl_function->z = z;
    
    [function_name release];
    
    [source release];
    
    [library release];
    
    [device release];
    
    return (PyObject*)py_mtl_function;
}

static PyMethodDef compushady_backends_metal_methods[] = {
    {"msl_compile", (PyCFunction)compushady_msl_compile, METH_VARARGS, "Compile a MSL shader"},
    {"enable_debug", (PyCFunction)metal_enable_debug, METH_NOARGS, "Enable GPU debug mode"},
    {"get_shader_binary_type", (PyCFunction)metal_get_shader_binary_type, METH_NOARGS, "Returns the required shader binary type"},
    {"get_discovered_devices", (PyCFunction)metal_get_discovered_devices, METH_NOARGS, "Returns the list of discovered GPU devices"},
    {"create_metal_layer", (PyCFunction)compushady_create_metal_layer, METH_VARARGS, "Creates a CAMetalLayer on the default Device"},
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
                                          &metal_Compute_Type, NULL, metal_Compute_methods,
                                          &metal_Sampler_Type, NULL, NULL
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
    
    
    MTL_FORMAT(R32G32B32A32_FLOAT, MTLPixelFormatRGBA32Float,  4 * 4);
    MTL_FORMAT(R32G32B32A32_UINT, MTLPixelFormatRGBA32Uint, 4 * 4);
    MTL_FORMAT(R32G32B32A32_SINT, MTLPixelFormatRGBA32Sint, 4 * 4);
    MTL_FORMAT(R16G16B16A16_FLOAT,MTLPixelFormatRGBA16Float, 4 * 2);
    MTL_FORMAT(R16G16B16A16_UNORM, MTLPixelFormatRGBA16Unorm, 4 * 2);
    MTL_FORMAT(R16G16B16A16_UINT, MTLPixelFormatRGBA16Uint, 4 * 2);
    MTL_FORMAT(R16G16B16A16_SNORM, MTLPixelFormatRGBA16Snorm, 4 * 2);
    MTL_FORMAT(R16G16B16A16_SINT, MTLPixelFormatRGBA16Sint, 4 * 2);
    MTL_FORMAT(R32G32_FLOAT,MTLPixelFormatRG32Float, 2 * 4);
    MTL_FORMAT(R32G32_UINT, MTLPixelFormatRG32Uint, 2 * 4);
    MTL_FORMAT(R32G32_SINT, MTLPixelFormatRG32Sint, 2 * 4);
    MTL_FORMAT(R8G8B8A8_UNORM, MTLPixelFormatRGBA8Unorm, 4);
    MTL_FORMAT(R8G8B8A8_UNORM_SRGB, MTLPixelFormatRGBA8Unorm_sRGB, 4);
    MTL_FORMAT(R8G8B8A8_UINT, MTLPixelFormatRGBA8Uint, 4);
    MTL_FORMAT(R8G8B8A8_SNORM, MTLPixelFormatRGBA8Snorm, 4);
    MTL_FORMAT(R8G8B8A8_SINT, MTLPixelFormatRGBA8Sint, 4);
    MTL_FORMAT(R16G16_FLOAT, MTLPixelFormatRG16Float, 2 * 2);
    MTL_FORMAT(R16G16_UNORM, MTLPixelFormatRG16Unorm, 2 * 2);
    MTL_FORMAT(R16G16_UINT, MTLPixelFormatRG16Uint, 2 * 2);
    MTL_FORMAT(R16G16_SNORM, MTLPixelFormatRG16Snorm, 2 * 2);
    MTL_FORMAT(R16G16_SINT, MTLPixelFormatRG16Sint, 2 * 2);
    MTL_FORMAT(R32_FLOAT, MTLPixelFormatR32Float, 4);
    MTL_FORMAT(R32_UINT, MTLPixelFormatR32Uint, 4);
    MTL_FORMAT(R32_SINT, MTLPixelFormatR32Sint, 4);
    MTL_FORMAT(R8G8_UNORM, MTLPixelFormatRG8Unorm, 2);
    MTL_FORMAT(R8G8_UINT, MTLPixelFormatRG8Uint, 2);
    MTL_FORMAT(R8G8_SNORM, MTLPixelFormatRG8Snorm, 2);
    MTL_FORMAT(R8G8_SINT, MTLPixelFormatRG8Sint, 2);
    MTL_FORMAT(R16_FLOAT, MTLPixelFormatR16Float, 2);
    MTL_FORMAT(R16_UNORM, MTLPixelFormatR16Unorm, 2);
    MTL_FORMAT(R16_UINT, MTLPixelFormatR16Uint, 2);
    MTL_FORMAT(R16_SNORM, MTLPixelFormatR16Snorm, 2);
    MTL_FORMAT(R16_SINT, MTLPixelFormatR16Sint, 2);
    MTL_FORMAT(R8_UNORM, MTLPixelFormatR8Unorm, 1);
    MTL_FORMAT(R8_UINT, MTLPixelFormatR8Uint, 1);
    MTL_FORMAT(R8_SNORM, MTLPixelFormatR8Snorm, 1);
    MTL_FORMAT(R8_SINT, MTLPixelFormatR8Sint, 1);
    MTL_FORMAT(B8G8R8A8_UNORM, MTLPixelFormatBGRA8Unorm, 4);
    MTL_FORMAT(B8G8R8A8_UNORM_SRGB, MTLPixelFormatBGRA8Unorm_sRGB, 4);
    
    return m;
}

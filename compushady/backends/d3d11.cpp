#include <d3d11.h>
#include <dxgi1_4.h>

#include "compushady.h"

void dxgi_init_pixel_formats();
PyObject* d3d_generate_exception(PyObject* py_exc, HRESULT hr, const char* prefix);
extern std::unordered_map<int, size_t> dxgi_pixels_sizes;

static bool d3d11_debug = false;

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
	char is_discrete;
} d3d11_Device;

typedef struct d3d11_Resource
{
	PyObject_HEAD;
	ID3D11Resource* resource;
	ID3D11Resource* staging_resource;
	d3d11_Device* py_device;
	SIZE_T size;
	UINT width;
	UINT height;
	UINT depth;
	UINT row_pitch;
	UINT stride;
	UINT cpu_access_flags;
	DXGI_FORMAT format;
} d3d11_Resource;

typedef struct d3d11_Swapchain
{
	PyObject_HEAD;
	d3d11_Device* py_device;
	IDXGISwapChain3* swapchain;
	DXGI_SWAP_CHAIN_DESC1 desc;
	ID3D11Resource* backbuffer;
} d3d11_Swapchain;

typedef struct d3d11_Compute
{
	PyObject_HEAD;
	d3d11_Device* py_device;
	ID3D11ComputeShader* compute_shader;
	PyObject* py_cbv;
	PyObject* py_srv;
	PyObject* py_uav;
	std::vector<ID3D11Buffer*> cbv;
	std::vector<ID3D11ShaderResourceView*> srv;
	std::vector<ID3D11UnorderedAccessView*> uav;
} d3d11_Compute;

typedef struct d3d11_Sampler
{
	PyObject_HEAD;
	d3d11_Device* py_device;
} d3d11_Sampler;

static PyMemberDef d3d11_Device_members[] = {
	{"name", T_OBJECT_EX, offsetof(d3d11_Device, name), 0, "device name/description"},
	{"dedicated_video_memory", T_ULONGLONG, offsetof(d3d11_Device, dedicated_video_memory), 0, "device dedicated video memory amount"},
	{"dedicated_system_memory", T_ULONGLONG, offsetof(d3d11_Device, dedicated_system_memory), 0, "device dedicated system memory amount"},
	{"shared_system_memory", T_ULONGLONG, offsetof(d3d11_Device, shared_system_memory), 0, "device shared system memory amount"},
	{"vendor_id", T_UINT, offsetof(d3d11_Device, vendor_id), 0, "device VendorId"},
	{"device_id", T_UINT, offsetof(d3d11_Device, vendor_id), 0, "device DeviceId"},
	{"is_hardware", T_BOOL, offsetof(d3d11_Device, is_hardware), 0, "returns True if this is a hardware device and not a emulated/software one"},
	{"is_discrete", T_BOOL, offsetof(d3d11_Device, is_discrete), 0, "returns True if this is a discrete device"},
	{NULL}  /* Sentinel */
};

static void d3d11_Device_dealloc(d3d11_Device* self)
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

static d3d11_Device* d3d11_Device_get_device(d3d11_Device* self)
{
	if (self->device)
		return self;

	HRESULT hr = D3D11CreateDevice(self->adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, d3d11_debug ? D3D11_CREATE_DEVICE_DEBUG : 0, NULL, 0, D3D11_SDK_VERSION, &self->device, NULL, &self->context);
	if (hr != S_OK)
	{
		d3d_generate_exception(PyExc_Exception, hr, "unable to create ID3D11Device");
		return NULL;
	}

	return self;
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

	if (self->staging_resource)
	{
		self->staging_resource->Release();
	}

	Py_XDECREF(self->py_device);

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

static void d3d11_Swapchain_dealloc(d3d11_Swapchain* self)
{
	if (self->swapchain)
	{
		self->swapchain->Release();
	}

	Py_XDECREF(self->py_device);

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject d3d11_Swapchain_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.d3d11.Swapchain", /* tp_name */
	sizeof(d3d11_Device),                                              /* tp_basicsize */
	0,                                                                 /* tp_itemsize */
	(destructor)d3d11_Swapchain_dealloc,                                  /* tp_dealloc */
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
	"compushady d3d11 Swapchain",                                         /* tp_doc */
};

static void d3d11_Compute_dealloc(d3d11_Compute* self)
{
	for (ID3D11ShaderResourceView* srv : self->srv)
	{
		srv->Release();
	}

	for (ID3D11UnorderedAccessView* uav : self->uav)
	{
		uav->Release();
	}

	Py_XDECREF(self->py_cbv);
	Py_XDECREF(self->py_srv);
	Py_XDECREF(self->py_uav);

	if (self->compute_shader)
	{
		self->compute_shader->Release();
	}

	Py_XDECREF(self->py_device);

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

static PyObject* d3d11_Swapchain_present(d3d11_Swapchain* self, PyObject* args)
{
	PyObject* py_resource;
	uint32_t x;
	uint32_t y;
	if (!PyArg_ParseTuple(args, "OII", &py_resource, &x, &y))
		return NULL;

	int ret = PyObject_IsInstance(py_resource, (PyObject*)&d3d11_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}
	d3d11_Resource* src_resource = (d3d11_Resource*)py_resource;
	if (src_resource->width == 0 || src_resource->height == 0 || src_resource->depth == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Texture object");
	}

	UINT index = self->swapchain->GetCurrentBackBufferIndex();

	x = Py_MIN(x, self->desc.Width - 1);
	y = Py_MIN(y, self->desc.Height - 1);


	D3D11_BOX box = {};
	box.right = Py_MIN(src_resource->width, self->desc.Width - x);
	box.bottom = Py_MIN(src_resource->height, self->desc.Height - y);
	box.back = 1;
	self->py_device->context->CopySubresourceRegion(self->backbuffer, 0, x, y, 0, src_resource->resource, 0, &box);

	HRESULT hr = self->swapchain->Present(1, 0);
	if (hr != S_OK)
	{
		return d3d_generate_exception(PyExc_Exception, hr, "unable to Present() Swapchain");
	}

	Py_RETURN_NONE;
}

static PyMethodDef d3d11_Swapchain_methods[] = {
	{"present", (PyCFunction)d3d11_Swapchain_present, METH_VARARGS, "Blit a texture resource to the Swapchain and present it"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};


static PyObject* d3d11_Device_create_buffer(d3d11_Device* self, PyObject* args)
{
	int heap;
	SIZE_T size;
	UINT stride;
	DXGI_FORMAT format;
	if (!PyArg_ParseTuple(args, "iKIi", &heap, &size, &stride, &format))
		return NULL;

	if (format > 0)
	{
		if (dxgi_pixels_sizes.find(format) == dxgi_pixels_sizes.end())
		{
			return PyErr_Format(PyExc_ValueError, "invalid pixel format");
		}
	}

	if (!size)
		return PyErr_Format(Compushady_BufferError, "zero size buffer");

	d3d11_Device* py_device = d3d11_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D11_BUFFER_DESC buffer_desc = {};

	switch (heap)
	{
	case COMPUSHADY_HEAP_DEFAULT:
		buffer_desc.Usage = D3D11_USAGE_DEFAULT;
		if (format == 0 && stride == 0 && size % 16 == 0) // constant buffer must be 16 bytes aligned
		{
			buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		}
		else
		{
			buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		}
		break;
	case COMPUSHADY_HEAP_UPLOAD:
		buffer_desc.Usage = D3D11_USAGE_STAGING;
		buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		break;
	case COMPUSHADY_HEAP_READBACK:
		buffer_desc.Usage = D3D11_USAGE_STAGING;
		buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		break;
	default:
		return PyErr_Format(PyExc_ValueError, "invalid heap type: %d", heap);
	}

	buffer_desc.ByteWidth = (UINT)size;
	buffer_desc.StructureByteStride = stride;
	if (stride > 0)
	{
		buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	}

	ID3D11Buffer* buffer;
	HRESULT hr = py_device->device->CreateBuffer(&buffer_desc, NULL, &buffer);
	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_BufferError, hr, "unable to create ID3D11Buffer");
	}

	d3d11_Resource* py_resource = (d3d11_Resource*)PyObject_New(d3d11_Resource, &d3d11_Resource_Type);
	if (!py_resource)
	{
		buffer->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d11 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = buffer;
	py_resource->size = size;
	py_resource->cpu_access_flags = buffer_desc.CPUAccessFlags;
	py_resource->format = format;
	py_resource->stride = stride;

	return (PyObject*)py_resource;
}

static PyObject* d3d11_Device_create_texture2d(d3d11_Device* self, PyObject* args)
{
	UINT width;
	UINT height;
	DXGI_FORMAT format;
	if (!PyArg_ParseTuple(args, "IIi", &width, &height, &format))
		return NULL;

	if (dxgi_pixels_sizes.find(format) == dxgi_pixels_sizes.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	d3d11_Device* py_device = d3d11_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D11_TEXTURE2D_DESC texture2d_desc = {};
	texture2d_desc.Width = width;
	texture2d_desc.Height = height;
	texture2d_desc.Usage = D3D11_USAGE_DEFAULT;
	texture2d_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	texture2d_desc.ArraySize = 1;
	texture2d_desc.MipLevels = 1;
	texture2d_desc.Format = format;
	texture2d_desc.SampleDesc.Count = 1;

	ID3D11Texture2D* texture;
	HRESULT hr = py_device->device->CreateTexture2D(&texture2d_desc, NULL, &texture);
	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_Texture2DError, hr, "Unable to create ID3D11Texture2D");
	}

	d3d11_Resource* py_resource = (d3d11_Resource*)PyObject_New(d3d11_Resource, &d3d11_Resource_Type);
	if (!py_resource)
	{
		texture->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d11 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = texture;
	py_resource->width = width;
	py_resource->height = height;
	py_resource->depth = 1;
	py_resource->row_pitch = (UINT)(width * dxgi_pixels_sizes[format]);
	py_resource->size = py_resource->row_pitch * height;
	py_resource->cpu_access_flags = texture2d_desc.CPUAccessFlags;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

static PyObject* d3d11_Device_create_texture1d(d3d11_Device* self, PyObject* args)
{
	UINT width;
	DXGI_FORMAT format;
	if (!PyArg_ParseTuple(args, "Ii", &width, &format))
		return NULL;

	if (dxgi_pixels_sizes.find(format) == dxgi_pixels_sizes.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	d3d11_Device* py_device = d3d11_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D11_TEXTURE1D_DESC texture1d_desc = {};
	texture1d_desc.Width = width;
	texture1d_desc.Usage = D3D11_USAGE_DEFAULT;
	texture1d_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	texture1d_desc.ArraySize = 1;
	texture1d_desc.MipLevels = 1;
	texture1d_desc.Format = format;

	ID3D11Texture1D* texture;
	HRESULT hr = py_device->device->CreateTexture1D(&texture1d_desc, NULL, &texture);
	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_Texture2DError, hr, "Unable to create ID3D11Texture1D");
	}

	d3d11_Resource* py_resource = (d3d11_Resource*)PyObject_New(d3d11_Resource, &d3d11_Resource_Type);
	if (!py_resource)
	{
		texture->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d11 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = texture;
	py_resource->width = width;
	py_resource->height = 1;
	py_resource->depth = 1;
	py_resource->row_pitch = (UINT)(width * dxgi_pixels_sizes[format]);
	py_resource->size = py_resource->row_pitch;
	py_resource->cpu_access_flags = texture1d_desc.CPUAccessFlags;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

static PyObject* d3d11_Device_create_texture3d(d3d11_Device* self, PyObject* args)
{
	UINT width;
	UINT height;
	UINT depth;
	DXGI_FORMAT format;
	if (!PyArg_ParseTuple(args, "IIIi", &width, &height, &depth, &format))
		return NULL;

	if (dxgi_pixels_sizes.find(format) == dxgi_pixels_sizes.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	d3d11_Device* py_device = d3d11_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D11_TEXTURE3D_DESC texture3d_desc = {};
	texture3d_desc.Width = width;
	texture3d_desc.Height = height;
	texture3d_desc.Depth = depth;
	texture3d_desc.Usage = D3D11_USAGE_DEFAULT;
	texture3d_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	texture3d_desc.MipLevels = 1;
	texture3d_desc.Format = format;

	ID3D11Texture3D* texture;
	HRESULT hr = py_device->device->CreateTexture3D(&texture3d_desc, NULL, &texture);
	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_Texture2DError, hr, "Unable to create ID3D11Texture3D");
	}

	d3d11_Resource* py_resource = (d3d11_Resource*)PyObject_New(d3d11_Resource, &d3d11_Resource_Type);
	if (!py_resource)
	{
		texture->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d11 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = texture;
	py_resource->width = width;
	py_resource->height = height;
	py_resource->depth = depth;
	py_resource->row_pitch = (UINT)(width * dxgi_pixels_sizes[format]);
	py_resource->size = py_resource->row_pitch * height * depth;
	py_resource->cpu_access_flags = texture3d_desc.CPUAccessFlags;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

static PyObject* d3d11_Device_create_texture2d_from_native(d3d11_Device* self, PyObject* args)
{
	unsigned long long texture_ptr;
	uint32_t width;
	uint32_t height;
	DXGI_FORMAT format;
	if (!PyArg_ParseTuple(args, "KIIi", &texture_ptr, &width, &height, &format))
		return NULL;

	if (dxgi_pixels_sizes.find(format) == dxgi_pixels_sizes.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	ID3D11Resource* resource = (ID3D11Resource*)texture_ptr;
	D3D11_RESOURCE_DIMENSION dimension;
	resource->GetType(&dimension);
	if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
	{
		return PyErr_Format(PyExc_ValueError, "supplied resource has the wrong Dimension (expected: D3D11_RESOURCE_DIMENSION_TEXTURE2D)");
	}
	d3d11_Resource* py_resource = (d3d11_Resource*)PyObject_New(d3d11_Resource, &d3d11_Resource_Type);
	if (!py_resource)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = self;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = resource;
	py_resource->resource->AddRef(); // keep the resource alive after python GC
	py_resource->width = width;
	py_resource->height = height;
	py_resource->depth = 1;
	py_resource->row_pitch = (UINT)(width * dxgi_pixels_sizes[format]);
	py_resource->size = py_resource->row_pitch * height;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

static PyObject* d3d11_Device_get_debug_messages(d3d11_Device* self, PyObject* args)
{
	d3d11_Device* py_device = d3d11_Device_get_device(self);
	if (!py_device)
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
			PyObject* py_debug_message = PyUnicode_FromStringAndSize(message->pDescription, message->DescriptionByteLength - 1);
			PyMem_Free(message);
			PyList_Append(py_list, py_debug_message);
			Py_DECREF(py_debug_message);
		}
		queue->ClearStoredMessages();
	}

	return py_list;
}

static PyObject* d3d11_Device_create_compute(d3d11_Device* self, PyObject* args, PyObject* kwds)
{
	const char* kwlist[] = { "shader", "cbv", "srv", "uav", "samplers", NULL };
	Py_buffer view;
	PyObject* py_cbv = NULL;
	PyObject* py_srv = NULL;
	PyObject* py_uav = NULL;
	PyObject* py_samplers = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*|OOOO", (char**)kwlist,
		&view, &py_cbv, &py_srv, &py_uav))
		return NULL;

	d3d11_Device* py_device = d3d11_Device_get_device(self);
	if (!py_device)
		return NULL;

	std::vector<d3d11_Resource*> cbv;
	std::vector<d3d11_Resource*> srv;
	std::vector<d3d11_Resource*> uav;
	std::vector<d3d11_Sampler*> samplers;

	if (!compushady_check_descriptors(&d3d11_Resource_Type, py_cbv, cbv, py_srv, srv, py_uav, uav, NULL, py_samplers, samplers))
	{
		PyBuffer_Release(&view);
		return NULL;
	}

	d3d11_Compute* py_compute = (d3d11_Compute*)PyObject_New(d3d11_Compute, &d3d11_Compute_Type);
	if (!py_compute)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d11 Compute");
	}
	COMPUSHADY_CLEAR(py_compute);
	py_compute->py_device = py_device;
	Py_INCREF(py_compute->py_device);

	HRESULT hr = py_device->device->CreateComputeShader(view.buf, view.len, NULL, &py_compute->compute_shader);
	PyBuffer_Release(&view);
	if (hr != S_OK)
	{
		Py_DECREF(py_compute);
		return d3d_generate_exception(PyExc_Exception, hr, "unable to create Compute Shader");
	}

	py_compute->cbv = {};
	py_compute->srv = {};
	py_compute->uav = {};

	py_compute->py_cbv = PyList_New(0);
	py_compute->py_srv = PyList_New(0);
	py_compute->py_uav = PyList_New(0);

	for (d3d11_Resource* py_resource : cbv)
	{
		py_compute->cbv.push_back((ID3D11Buffer*)py_resource->resource);
		PyList_Append(py_compute->py_cbv, (PyObject*)py_resource);
	}

	for (d3d11_Resource* py_resource : srv)
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc_ptr = NULL;
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		bool is_buffer = py_resource->width == 0 && py_resource->height == 0 && py_resource->depth == 0;
		if (is_buffer && (py_resource->format > 0 || py_resource->stride > 0))
		{
			srv_desc.Format = py_resource->format;
			srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srv_desc.Buffer.NumElements = (UINT)(py_resource->stride > 0 ? py_resource->size / py_resource->stride : py_resource->size / dxgi_pixels_sizes[py_resource->format]);
			srv_desc_ptr = &srv_desc;
		}
		ID3D11ShaderResourceView* new_srv;
		HRESULT hr = py_device->device->CreateShaderResourceView(py_resource->resource, srv_desc_ptr, &new_srv);
		if (hr != S_OK)
		{
			Py_DECREF(py_compute);
			return d3d_generate_exception(PyExc_Exception, hr, "unable to create Shader Resource View");
		}
		py_compute->srv.push_back(new_srv);
		PyList_Append(py_compute->py_srv, (PyObject*)py_resource);
	}

	for (d3d11_Resource* py_resource : uav)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc_ptr = NULL;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
		bool is_buffer = py_resource->width == 0 && py_resource->height == 0 && py_resource->depth == 0;
		if (is_buffer && (py_resource->format > 0 || py_resource->stride > 0))
		{
			uav_desc.Format = py_resource->format;
			uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			uav_desc.Buffer.NumElements = (UINT)(py_resource->stride > 0 ? py_resource->size / py_resource->stride : py_resource->size / dxgi_pixels_sizes[py_resource->format]);
			uav_desc_ptr = &uav_desc;
		}
		ID3D11UnorderedAccessView* new_uav;
		HRESULT hr = py_device->device->CreateUnorderedAccessView(py_resource->resource, uav_desc_ptr, &new_uav);
		if (hr != S_OK)
		{
			Py_DECREF(py_compute);
			return d3d_generate_exception(PyExc_Exception, hr, "unable to create Unordered Access View");
		}
		py_compute->uav.push_back(new_uav);
		PyList_Append(py_compute->py_uav, (PyObject*)py_resource);
	}

	return (PyObject*)py_compute;
}

static PyObject* d3d11_Device_create_swapchain(d3d11_Device* self, PyObject* args)
{
	HWND window_handle;
	DXGI_FORMAT format;
	uint32_t num_buffers;
	if (!PyArg_ParseTuple(args, "KiI", &window_handle, &format, &num_buffers))
		return NULL;

	d3d11_Device* py_device = d3d11_Device_get_device(self);
	if (!py_device)
		return NULL;

	d3d11_Swapchain* py_swapchain = (d3d11_Swapchain*)PyObject_New(d3d11_Swapchain, &d3d11_Swapchain_Type);
	if (!py_swapchain)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d11 Swapchain");
	}
	COMPUSHADY_CLEAR(py_swapchain);
	py_swapchain->py_device = py_device;
	Py_INCREF(py_swapchain->py_device);

	IDXGIFactory2* factory = NULL;
	HRESULT hr = CreateDXGIFactory2(d3d11_debug ? DXGI_CREATE_FACTORY_DEBUG : 0, __uuidof(IDXGIFactory2), (void**)&factory);
	if (hr != S_OK)
	{
		Py_DECREF(py_swapchain);
		return d3d_generate_exception(PyExc_Exception, hr, "unable to create IDXGIFactory2");
	}

	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc1 = {};
	swap_chain_desc1.Format = format;
	swap_chain_desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc1.BufferCount = num_buffers;
	swap_chain_desc1.Scaling = DXGI_SCALING_STRETCH;
	swap_chain_desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc1.SampleDesc.Count = 1;
	swap_chain_desc1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

	hr = factory->CreateSwapChainForHwnd(py_device->device, window_handle, &swap_chain_desc1, NULL, NULL, (IDXGISwapChain1**)&py_swapchain->swapchain);
	if (hr != S_OK)
	{
		factory->Release();
		Py_DECREF(py_swapchain);
		return d3d_generate_exception(PyExc_Exception, hr, "unable to create Swapchain");
	}

	py_swapchain->swapchain->GetDesc1(&py_swapchain->desc);

	hr = py_swapchain->swapchain->GetBuffer(0, __uuidof(ID3D11Resource), (void**)&py_swapchain->backbuffer);
	if (hr != S_OK)
	{
		factory->Release();
		Py_DECREF(py_swapchain);
		return d3d_generate_exception(PyExc_Exception, hr, "unable to get Swapchain buffer");
	}

	factory->Release();

	return (PyObject*)py_swapchain;
}

static PyMethodDef d3d11_Device_methods[] = {
	{"create_buffer", (PyCFunction)d3d11_Device_create_buffer, METH_VARARGS, "Creates a Buffer object"},
	{"create_texture2d", (PyCFunction)d3d11_Device_create_texture2d, METH_VARARGS, "Creates a Texture2D object"},
	{"create_texture1d", (PyCFunction)d3d11_Device_create_texture1d, METH_VARARGS, "Creates a Texture1D object"},
	{"create_texture3d", (PyCFunction)d3d11_Device_create_texture3d, METH_VARARGS, "Creates a Texture3D object"},
	{"create_texture2d_from_native", (PyCFunction)d3d11_Device_create_texture2d_from_native, METH_VARARGS, "Creates a Texture2D object from a low level pointer"},
	{"get_debug_messages", (PyCFunction)d3d11_Device_get_debug_messages, METH_VARARGS, "Get Device's debug messages"},
	{"create_compute", (PyCFunction)d3d11_Device_create_compute, METH_VARARGS | METH_KEYWORDS, "Creates a Compute object"},
	{"create_swapchain", (PyCFunction)d3d11_Device_create_swapchain, METH_VARARGS, "Creates a Swapchain object"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* d3d11_Resource_upload(d3d11_Resource* self, PyObject* args)
{
	Py_buffer view;
	SIZE_T offset;
	if (!PyArg_ParseTuple(args, "y*K", &view, &offset))
		return NULL;

	if (offset + (SIZE_T)view.len > self->size)
	{
		size_t size = view.len;
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_ValueError, "supplied buffer is bigger than resource size: %llu (expected no more than %llu)", size, self->size);
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = self->py_device->context->Map(self->resource, 0, D3D11_MAP_WRITE, 0, &mapped);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		return d3d_generate_exception(PyExc_Exception, hr, "unable to Map() ID3D11Resource");
	}

	memcpy((char*)mapped.pData + offset, view.buf, view.len);
	self->py_device->context->Unmap(self->resource, 0);
	PyBuffer_Release(&view);

	Py_RETURN_NONE;
}

static PyObject* d3d11_Resource_upload2d(d3d11_Resource* self, PyObject* args)
{
	Py_buffer view;
	UINT pitch;
	UINT width;
	UINT height;
	UINT bytes_per_pixel;
	if (!PyArg_ParseTuple(args, "y*IIII", &view, &pitch, &width, &height, &bytes_per_pixel))
		return NULL;

	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = self->py_device->context->Map(self->resource, 0, D3D11_MAP_WRITE, 0, &mapped);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		return d3d_generate_exception(PyExc_Exception, hr, "unable to Map() ID3D11Resource");
	}

	size_t offset = 0;
	size_t remains = view.len;
	size_t resource_remains = self->size;
	for (UINT y = 0; y < height; y++)
	{
		size_t amount = Py_MIN(width * bytes_per_pixel, Py_MIN(remains, resource_remains));
		memcpy((char*)mapped.pData + (pitch * y), (char*)view.buf + offset, amount);
		remains -= amount;
		if (remains == 0)
			break;
		resource_remains -= amount;
		offset += amount;
	}

	self->py_device->context->Unmap(self->resource, 0);
	PyBuffer_Release(&view);

	Py_RETURN_NONE;
}

static PyObject* d3d11_Resource_readback(d3d11_Resource* self, PyObject* args)
{
	SIZE_T size;
	SIZE_T offset;
	if (!PyArg_ParseTuple(args, "KK", &size, &offset))
		return NULL;

	if (size == 0)
		size = self->size - offset;

	if (offset + size > self->size)
	{
		return PyErr_Format(PyExc_ValueError, "requested buffer out of bounds: (offset %llu) %llu (expected no more than %llu)", offset, size, self->size);
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = self->py_device->context->Map(self->resource, 0, D3D11_MAP_READ, 0, &mapped);
	if (hr != S_OK)
	{
		return d3d_generate_exception(PyExc_Exception, hr, "unable to Map() ID3D11Resource");
	}

	PyObject* py_bytes = PyBytes_FromStringAndSize((const char*)mapped.pData + offset, size);
	self->py_device->context->Unmap(self->resource, 0);
	return py_bytes;
}

static PyObject* d3d11_Resource_readback_to_buffer(d3d11_Resource* self, PyObject* args)
{
	Py_buffer view;
	SIZE_T offset = 0;
	if (!PyArg_ParseTuple(args, "y*K", &view, &offset))
		return NULL;

	if (offset > self->size)
	{
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_ValueError, "requested buffer out of bounds: %llu (expected no more than %llu)", offset, self->size);
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = self->py_device->context->Map(self->resource, 0, D3D11_MAP_READ, 0, &mapped);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		return d3d_generate_exception(PyExc_Exception, hr, "unable to Map() ID3D11Resource");
	}

	memcpy(view.buf, (char*)mapped.pData + offset, Py_MIN((SIZE_T)view.len, self->size - offset));

	self->py_device->context->Unmap(self->resource, 0);
	Py_RETURN_NONE;
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

	bool src_is_buffer = self->width == 0 && self->height == 0 && self->depth == 0;
	bool dst_is_buffer = dst_resource->width == 0 && dst_resource->height == 0 && dst_resource->depth == 0;

	if (src_is_buffer && dst_is_buffer)
	{
		self->py_device->context->CopyResource(dst_resource->resource, self->resource);
	}
	else if (src_is_buffer)
	{
		if (self->cpu_access_flags & D3D11_CPU_ACCESS_READ)
		{
			D3D11_MAPPED_SUBRESOURCE mapped;
			HRESULT hr = self->py_device->context->Map(self->resource, 0, D3D11_MAP_READ, 0, &mapped);
			if (hr != S_OK)
			{
				return PyErr_Format(PyExc_Exception, "unable to Map() source buffer");
			}
			self->py_device->context->UpdateSubresource(dst_resource->resource, 0, NULL, mapped.pData, dst_resource->row_pitch, dst_resource->row_pitch * dst_resource->height);
			self->py_device->context->Unmap(self->resource, 0);
		}
		else
		{
			// staging buffer...
			if (!self->staging_resource)
			{
				D3D11_BUFFER_DESC buffer_desc = {};
				buffer_desc.Usage = D3D11_USAGE_STAGING;
				buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				buffer_desc.ByteWidth = (UINT)self->size;
				HRESULT hr = self->py_device->device->CreateBuffer(&buffer_desc, NULL, (ID3D11Buffer**)&self->staging_resource);
				if (hr != S_OK)
				{
					return PyErr_Format(PyExc_Exception, "unable to create the staging buffer");
				}
			}
			self->py_device->context->CopyResource(self->staging_resource, self->resource);
			D3D11_MAPPED_SUBRESOURCE mapped;
			HRESULT hr = self->py_device->context->Map(self->staging_resource, 0, D3D11_MAP_READ, 0, &mapped);
			if (hr != S_OK)
			{
				return PyErr_Format(PyExc_Exception, "unable to Map() staging buffer");
			}
			self->py_device->context->UpdateSubresource(dst_resource->resource, 0, NULL, mapped.pData, dst_resource->row_pitch, dst_resource->row_pitch * dst_resource->height);
			self->py_device->context->Unmap(self->staging_resource, 0);
		}
	}
	else if (dst_is_buffer)
	{
		// staging buffer...
		if (!self->staging_resource)
		{
			D3D11_RESOURCE_DIMENSION dimension;
			self->resource->GetType(&dimension);
			HRESULT hr = E_FAIL;
			if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
			{
				D3D11_TEXTURE1D_DESC texture1d_desc = {};
				texture1d_desc.Width = self->width;
				texture1d_desc.Usage = D3D11_USAGE_STAGING;
				texture1d_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				texture1d_desc.ArraySize = 1;
				texture1d_desc.MipLevels = 1;
				texture1d_desc.Format = self->format;
				hr = self->py_device->device->CreateTexture1D(&texture1d_desc, NULL, (ID3D11Texture1D**)&self->staging_resource);
			}
			else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
			{
				D3D11_TEXTURE2D_DESC texture2d_desc = {};
				texture2d_desc.Width = self->width;
				texture2d_desc.Height = self->height;
				texture2d_desc.Usage = D3D11_USAGE_STAGING;
				texture2d_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				texture2d_desc.ArraySize = 1;
				texture2d_desc.MipLevels = 1;
				texture2d_desc.Format = self->format;
				texture2d_desc.SampleDesc.Count = 1;
				hr = self->py_device->device->CreateTexture2D(&texture2d_desc, NULL, (ID3D11Texture2D**)&self->staging_resource);
			}
			else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
			{
				D3D11_TEXTURE3D_DESC texture3d_desc = {};
				texture3d_desc.Width = self->width;
				texture3d_desc.Height = self->height;
				texture3d_desc.Depth = self->depth;
				texture3d_desc.Usage = D3D11_USAGE_STAGING;
				texture3d_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				texture3d_desc.MipLevels = 1;
				texture3d_desc.Format = self->format;
				hr = self->py_device->device->CreateTexture3D(&texture3d_desc, NULL, (ID3D11Texture3D**)&self->staging_resource);
			}

			if (hr != S_OK)
			{
				return PyErr_Format(PyExc_Exception, "unable to create the staging texture");
			}
		}
		self->py_device->context->CopyResource(self->staging_resource, self->resource);

		D3D11_MAPPED_SUBRESOURCE mapped;
		HRESULT hr = self->py_device->context->Map(self->staging_resource, 0, D3D11_MAP_READ, 0, &mapped);
		if (hr != S_OK)
		{
			return PyErr_Format(PyExc_Exception, "unable to Map() staging buffer");
		}
		char* data = (char*)mapped.pData;
		if (mapped.RowPitch != self->row_pitch || mapped.DepthPitch != self->row_pitch * self->height)
		{
			data = (char*)PyMem_Malloc(self->row_pitch * self->height * self->depth);
			if (!data)
			{
				self->py_device->context->Unmap(self->staging_resource, 0);
				return PyErr_Format(PyExc_MemoryError, "unable to create the staging data buffer");
			}
			char* src_offset = (char*)mapped.pData;
			for (UINT depth = 0; depth < self->depth; depth++)
			{
				char* dst_offset = data + (self->row_pitch * self->height) * depth;
				for (UINT height = 0; height < self->height; height++)
				{
					memcpy(dst_offset + (self->row_pitch * height), src_offset + (mapped.RowPitch * height), self->row_pitch);
				}
				src_offset += mapped.DepthPitch;
			}
		}

		if (dst_resource->cpu_access_flags & D3D11_CPU_ACCESS_WRITE)
		{
			D3D11_MAPPED_SUBRESOURCE write_mapped;
			hr = self->py_device->context->Map(dst_resource->resource, 0, D3D11_MAP_WRITE, 0, &write_mapped);
			if (hr != S_OK)
			{
				self->py_device->context->Unmap(self->staging_resource, 0);
				if (mapped.pData != data)
				{
					PyMem_Free(data);
				}
				return PyErr_Format(PyExc_MemoryError, "unable to Map() destination buffer");
			}
			memcpy(write_mapped.pData, data, Py_MIN(dst_resource->size, self->row_pitch * self->height * self->depth));
			self->py_device->context->Unmap(dst_resource->resource, 0);
		}
		else
		{
			self->py_device->context->UpdateSubresource(dst_resource->resource, 0, NULL, data, self->row_pitch, self->row_pitch * self->depth);
		}
		self->py_device->context->Unmap(self->staging_resource, 0);
		if (mapped.pData != data)
		{
			PyMem_Free(data);
		}
	}
	else
	{
		self->py_device->context->CopySubresourceRegion(dst_resource->resource, 0, 0, 0, 0, self->resource, 0, NULL);
	}

	Py_RETURN_NONE;
}

static PyMethodDef d3d11_Resource_methods[] = {
	{"upload", (PyCFunction)d3d11_Resource_upload, METH_VARARGS, "Upload bytes to a GPU Resource"},
	{"upload2d", (PyCFunction)d3d11_Resource_upload2d, METH_VARARGS, "Upload bytes to a GPU Resource given pitch, width, height and pixel size"},
	{"readback", (PyCFunction)d3d11_Resource_readback, METH_VARARGS, "Readback bytes from a GPU Resource"},
	{"readback_to_buffer", (PyCFunction)d3d11_Resource_readback_to_buffer, METH_VARARGS, "Readback into a buffer from a GPU Resource"},
	{"copy_to", (PyCFunction)d3d11_Resource_copy_to, METH_VARARGS, "Copy resource content to another resource"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyMemberDef d3d11_Resource_members[] = {
	{"size", T_ULONGLONG, offsetof(d3d11_Resource, size), 0, "resource size"},
	{"width", T_UINT, offsetof(d3d11_Resource, width), 0, "resource width"},
	{"height", T_UINT, offsetof(d3d11_Resource, height), 0, "resource height"},
	{"depth", T_UINT, offsetof(d3d11_Resource, depth), 0, "resource depth"},
	{"row_pitch", T_UINT, offsetof(d3d11_Resource, row_pitch), 0, "resource row pitch"},
	{NULL}  /* Sentinel */
};

static PyObject* d3d11_Compute_dispatch(d3d11_Compute* self, PyObject* args)
{
	UINT x, y, z;
	if (!PyArg_ParseTuple(args, "III", &x, &y, &z))
		return NULL;

	self->py_device->context->CSSetShader(self->compute_shader, NULL, 0);
	self->py_device->context->CSSetConstantBuffers(0, (UINT)self->cbv.size(), self->cbv.data());
	self->py_device->context->CSSetShaderResources(0, (UINT)self->srv.size(), self->srv.data());
	self->py_device->context->CSSetUnorderedAccessViews(0, (UINT)self->uav.size(), self->uav.data(), NULL);
	self->py_device->context->Dispatch(x, y, z);

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
		return d3d_generate_exception(PyExc_Exception, hr, "unable to create IDXGIFactory1");
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
			return d3d_generate_exception(PyExc_Exception, hr, "error while calling GetDesc1");
		}

		d3d11_Device* py_device = (d3d11_Device*)PyObject_New(d3d11_Device, &d3d11_Device_Type);
		if (!py_device)
		{
			Py_DECREF(py_list);
			return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d11 Device");
		}
		COMPUSHADY_CLEAR(py_device);

		py_device->adapter = adapter; // keep the com refcnt as is given that EnumAdapters1 increases it
		py_device->device = NULL;     // will be lazily allocated
		py_device->name = PyUnicode_FromWideChar(adapter_desc.Description, -1);
		py_device->dedicated_video_memory = adapter_desc.DedicatedVideoMemory;
		py_device->dedicated_system_memory = adapter_desc.DedicatedSystemMemory;
		py_device->shared_system_memory = adapter_desc.SharedSystemMemory;
		py_device->vendor_id = adapter_desc.VendorId;
		py_device->device_id = adapter_desc.DeviceId;
		py_device->is_hardware = (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0;
		py_device->is_discrete = py_device->is_hardware;

		PyList_Append(py_list, (PyObject*)py_device);
		Py_DECREF(py_device);
	}

	factory->Release();

	return py_list;
}

static PyObject* d3d11_enable_debug(PyObject* self)
{
	d3d11_debug = true;
	Py_RETURN_NONE;
}

static PyObject* d3d11_get_shader_binary_type(PyObject* self)
{
	return PyLong_FromLong(COMPUSHADY_SHADER_BINARY_TYPE_DXBC);
}

static PyMethodDef compushady_backends_d3d11_methods[] = {
	{"get_discovered_devices", (PyCFunction)d3d11_get_discovered_devices, METH_NOARGS, "Returns the list of discovered GPU devices"},
	{"enable_debug", (PyCFunction)d3d11_enable_debug, METH_NOARGS, "Enable GPU debug mode"},
	{"get_shader_binary_type", (PyCFunction)d3d11_get_shader_binary_type, METH_NOARGS, "Returns the required shader binary type"},
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
	PyObject* m = compushady_backend_init(
		&compushady_backends_d3d11_module,
		&d3d11_Device_Type, d3d11_Device_members, d3d11_Device_methods,
		&d3d11_Resource_Type, d3d11_Resource_members, d3d11_Resource_methods,
		&d3d11_Swapchain_Type, /*d3d11_Swapchain_members*/ NULL, d3d11_Swapchain_methods,
		&d3d11_Compute_Type, NULL, d3d11_Compute_methods,
		NULL, NULL, NULL
	);

	dxgi_init_pixel_formats();

	return m;
}

#include <d3d12.h>
#include <dxgi1_4.h>

#include "compushady.h"

void dxgi_init_pixel_formats();
PyObject* d3d_generate_exception(PyObject* py_exc, HRESULT hr, const char* prefix);
extern std::unordered_map<int, size_t> dxgi_pixels_sizes;

static bool d3d12_debug = false;

typedef struct d3d12_Device
{
	PyObject_HEAD;
	IDXGIAdapter1* adapter;
	ID3D12Device1* device;
	ID3D12CommandQueue* queue;
	ID3D12Fence1* fence;
	HANDLE fence_event;
	UINT64 fence_value;
	ID3D12CommandAllocator* command_allocator;
	ID3D12GraphicsCommandList1* command_list;
	PyObject* name;
	SIZE_T dedicated_video_memory;
	SIZE_T dedicated_system_memory;
	SIZE_T shared_system_memory;
	UINT vendor_id;
	UINT device_id;
	char is_hardware;
	char is_discrete;

} d3d12_Device;

typedef struct d3d12_Resource
{
	PyObject_HEAD;
	d3d12_Device* py_device;
	ID3D12Resource1* resource;
	SIZE_T size;
	UINT stride;
	DXGI_FORMAT format;
	D3D12_HEAP_TYPE heap_type;
	D3D12_RESOURCE_DIMENSION dimension;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
} d3d12_Resource;

typedef struct d3d12_Swapchain
{
	PyObject_HEAD;
	d3d12_Device* py_device;
	IDXGISwapChain3* swapchain;
	DXGI_SWAP_CHAIN_DESC1 desc;
	std::vector<ID3D12Resource*> backbuffers;
} d3d12_Swapchain;

typedef struct d3d12_Compute
{
	PyObject_HEAD;
	d3d12_Device* py_device;
	ID3D12RootSignature* root_signature;
	ID3D12DescriptorHeap* descriptor_heap;
	ID3D12PipelineState* pipeline;
} d3d12_Compute;

static PyMemberDef d3d12_Device_members[] = {
	{"name", T_OBJECT_EX, offsetof(d3d12_Device, name), 0, "device name/description"},
	{"dedicated_video_memory", T_ULONGLONG, offsetof(d3d12_Device, dedicated_video_memory), 0, "device dedicated video memory amount"},
	{"dedicated_system_memory", T_ULONGLONG, offsetof(d3d12_Device, dedicated_system_memory), 0, "device dedicated system memory amount"},
	{"shared_system_memory", T_ULONGLONG, offsetof(d3d12_Device, shared_system_memory), 0, "device shared system memory amount"},
	{"vendor_id", T_UINT, offsetof(d3d12_Device, vendor_id), 0, "device VendorId"},
	{"device_id", T_UINT, offsetof(d3d12_Device, vendor_id), 0, "device DeviceId"},
	{"is_hardware", T_BOOL, offsetof(d3d12_Device, is_hardware), 0, "returns True if this is a hardware device and not an emulated/software one"},
	{"is_discrete", T_BOOL, offsetof(d3d12_Device, is_discrete), 0, "returns True if this is a discrete device"},
	{NULL}  /* Sentinel */
};

static PyMemberDef d3d12_Resource_members[] = {
	{"size", T_ULONGLONG, offsetof(d3d12_Resource, size), 0, "resource size"},
	{"width", T_UINT, offsetof(d3d12_Resource, footprint) + offsetof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT, Footprint.Width), 0, "resource width"},
	{"height", T_UINT, offsetof(d3d12_Resource, footprint) + offsetof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT, Footprint.Height), 0, "resource height"},
	{"depth", T_UINT, offsetof(d3d12_Resource, footprint) + offsetof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT, Footprint.Depth), 0, "resource depth"},
	{"row_pitch", T_UINT, offsetof(d3d12_Resource, footprint) + offsetof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT, Footprint.RowPitch), 0, "resource row pitch"},
	{NULL}  /* Sentinel */
};

static void d3d12_Device_dealloc(d3d12_Device* self)
{
	Py_XDECREF(self->name);

	if (self->command_list)
	{
		self->command_list->Release();
	}

	if (self->command_allocator)
	{
		self->command_allocator->Release();
	}

	if (self->fence_event)
	{
		CloseHandle(self->fence_event);
	}

	if (self->fence)
	{
		self->fence->Release();
	}

	if (self->queue)
	{
		self->queue->Release();
	}

	if (self->device)
	{
		self->device->Release();
	}

	if (self->adapter)
	{
		self->adapter->Release();
	}

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static d3d12_Device* d3d12_Device_get_device(d3d12_Device* self)
{
	if (self->device)
		return self;

	HRESULT hr = D3D12CreateDevice(self->adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device1), (void**)&self->device);
	if (hr != S_OK)
	{
		d3d_generate_exception(PyExc_Exception, hr, "Unable to create ID3D12Device1");
		return NULL;
	}

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // we need this for swapchain support (TODO: maybe allow the user to configure this or fallback to compute)

	hr = self->device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), (void**)&self->queue);
	if (hr != S_OK)
	{
		self->device->Release();
		self->device = NULL;
		d3d_generate_exception(PyExc_Exception, hr, "Unable to create Command Queue");
		return NULL;
	}

	hr = self->device->CreateFence(self->fence_value, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence1), (void**)&self->fence);
	if (hr != S_OK)
	{
		self->queue->Release();
		self->queue = NULL;
		self->device->Release();
		self->device = NULL;
		d3d_generate_exception(PyExc_Exception, hr, "Unable to create Fence");
		return NULL;
	}

	self->fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	hr = self->device->CreateCommandAllocator(queue_desc.Type, __uuidof(ID3D12CommandAllocator), (void**)&self->command_allocator);
	if (hr != S_OK)
	{
		CloseHandle(self->fence_event);
		self->fence_event = NULL;
		self->fence->Release();
		self->fence = NULL;
		self->queue->Release();
		self->queue = NULL;
		self->device->Release();
		self->device = NULL;
		d3d_generate_exception(PyExc_Exception, hr, "Unable to create Command Allocator");
		return NULL;
	}

	hr = self->device->CreateCommandList(0, queue_desc.Type, self->command_allocator, NULL, __uuidof(ID3D12GraphicsCommandList1), (void**)&self->command_list);
	if (hr != S_OK)
	{
		self->command_allocator->Release();
		self->command_allocator = NULL;
		CloseHandle(self->fence_event);
		self->fence_event = NULL;
		self->fence->Release();
		self->fence = NULL;
		self->queue->Release();
		self->queue = NULL;
		self->device->Release();
		self->device = NULL;
		d3d_generate_exception(PyExc_Exception, hr, "Unable to create Command List");
		return NULL;
	}

	self->command_list->Close();

	return self;
}


static PyTypeObject d3d12_Device_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.d3d12.Device", /* tp_name */
	sizeof(d3d12_Device),                                              /* tp_basicsize */
	0,                                                                 /* tp_itemsize */
	(destructor)d3d12_Device_dealloc,                                  /* tp_dealloc */
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
	"compushady d3d12 Device",                                         /* tp_doc */
};

static void d3d12_Resource_dealloc(d3d12_Resource* self)
{
	if (self->resource)
	{
		self->resource->Release();
	}

	Py_XDECREF(self->py_device);

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject d3d12_Resource_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.d3d12.Resource", /* tp_name */
	sizeof(d3d12_Resource),                                              /* tp_basicsize */
	0,																	 /* tp_itemsize */
	(destructor)d3d12_Resource_dealloc,                                  /* tp_dealloc */
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
	"compushady d3d12 Resource",                                         /* tp_doc */
};

static void d3d12_Swapchain_dealloc(d3d12_Swapchain* self)
{
	if (self->swapchain)
	{
		self->swapchain->Release();
	}

	Py_XDECREF(self->py_device);

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject d3d12_Swapchain_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.d3d12.Swapchain", /* tp_name */
	sizeof(d3d12_Swapchain),                                              /* tp_basicsize */
	0,                                                                 /* tp_itemsize */
	(destructor)d3d12_Swapchain_dealloc,                                  /* tp_dealloc */
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
	"compushady d3d12 Swapchain",                                         /* tp_doc */
};

static void d3d12_Compute_dealloc(d3d12_Compute* self)
{

	if (self->pipeline)
		self->pipeline->Release();
	if (self->descriptor_heap)
		self->descriptor_heap->Release();
	if (self->root_signature)
		self->root_signature->Release();

	Py_XDECREF(self->py_device);

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject d3d12_Compute_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.d3d12.Compute",  /* tp_name */
	sizeof(d3d12_Compute),                                               /* tp_basicsize */
	0,																	 /* tp_itemsize */
	(destructor)d3d12_Compute_dealloc,                                   /* tp_dealloc */
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
	"compushady d3d12 Compute",                                          /* tp_doc */
};

static PyObject* d3d12_Swapchain_present(d3d12_Swapchain* self, PyObject* args)
{
	PyObject* py_resource;
	uint32_t x;
	uint32_t y;
	if (!PyArg_ParseTuple(args, "OII", &py_resource, &x, &y))
		return NULL;

	int ret = PyObject_IsInstance(py_resource, (PyObject*)&d3d12_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}
	d3d12_Resource* src_resource = (d3d12_Resource*)py_resource;
	if (src_resource->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Texture object");
	}

	UINT index = self->swapchain->GetCurrentBackBufferIndex();

	x = Py_MIN(x, self->desc.Width - 1);
	y = Py_MIN(y, self->desc.Height - 1);

	ID3D12Resource* backbuffer = self->backbuffers[index];

	self->py_device->command_list->Reset(self->py_device->command_allocator, NULL);

	D3D12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Transition.pResource = backbuffer;
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[1].Transition.pResource = src_resource->resource;
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

	self->py_device->command_list->ResourceBarrier(2, barriers);

	D3D12_TEXTURE_COPY_LOCATION dest_location = {};
	dest_location.pResource = backbuffer;
	dest_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

	D3D12_TEXTURE_COPY_LOCATION src_location = {};
	src_location.pResource = ((d3d12_Resource*)py_resource)->resource;
	src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

	D3D12_BOX box = {};
	box.right = Py_MIN(((d3d12_Resource*)py_resource)->footprint.Footprint.Width, self->desc.Width - x);
	box.bottom = Py_MIN(((d3d12_Resource*)py_resource)->footprint.Footprint.Height, self->desc.Height - y);
	box.back = 1;
	self->py_device->command_list->CopyTextureRegion(&dest_location, x, y, 0, &src_location, &box);

	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;

	self->py_device->command_list->ResourceBarrier(2, barriers);

	self->py_device->command_list->Close();

	self->py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&self->py_device->command_list);
	self->py_device->queue->Signal(self->py_device->fence, ++self->py_device->fence_value);
	self->py_device->fence->SetEventOnCompletion(self->py_device->fence_value, self->py_device->fence_event);
	WaitForSingleObject(self->py_device->fence_event, INFINITE);

	HRESULT hr = self->swapchain->Present(1, 0);
	if (hr != S_OK)
	{
		return d3d_generate_exception(PyExc_Exception, hr, "unable to Present() Swapchain");
	}

	Py_RETURN_NONE;
}

static PyMethodDef d3d12_Swapchain_methods[] = {
	{"present", (PyCFunction)d3d12_Swapchain_present, METH_VARARGS, "Blit a texture resource to the Swapchain and present it"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* d3d12_Device_create_swapchain(d3d12_Device* self, PyObject* args)
{
	HWND window_handle;
	DXGI_FORMAT format;
	uint32_t num_buffers;
	if (!PyArg_ParseTuple(args, "KiI", &window_handle, &format, &num_buffers))
		return NULL;

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	d3d12_Swapchain* py_swapchain = (d3d12_Swapchain*)PyObject_New(d3d12_Swapchain, &d3d12_Swapchain_Type);
	if (!py_swapchain)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Swapchain");
	}
	COMPUSHADY_CLEAR(py_swapchain);
	py_swapchain->py_device = py_device;
	Py_INCREF(py_swapchain->py_device);
	py_swapchain->backbuffers = {};

	IDXGIFactory2* factory = NULL;
	HRESULT hr = CreateDXGIFactory2(d3d12_debug ? DXGI_CREATE_FACTORY_DEBUG : 0, __uuidof(IDXGIFactory2), (void**)&factory);
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

	hr = factory->CreateSwapChainForHwnd(py_device->queue, window_handle, &swap_chain_desc1, NULL, NULL, (IDXGISwapChain1**)&py_swapchain->swapchain);
	if (hr != S_OK)
	{
		factory->Release();
		Py_DECREF(py_swapchain);
		return d3d_generate_exception(PyExc_Exception, hr, "unable to create Swapchain");
	}

	py_swapchain->swapchain->GetDesc1(&py_swapchain->desc);

	py_swapchain->backbuffers.resize(py_swapchain->desc.BufferCount);
	for (UINT i = 0; i < py_swapchain->desc.BufferCount; i++)
	{
		hr = py_swapchain->swapchain->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&py_swapchain->backbuffers.data()[i]);
		if (hr != S_OK)
		{
			factory->Release();
			Py_DECREF(py_swapchain);
			return d3d_generate_exception(PyExc_Exception, hr, "unable to get Swapchain buffer");
		}
	}

	factory->Release();

	return (PyObject*)py_swapchain;
}


static PyObject* d3d12_Device_create_buffer(d3d12_Device* self, PyObject* args)
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

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES heap_properties = {};
	switch (heap)
	{
	case COMPUSHADY_HEAP_DEFAULT:
		heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		break;
	case COMPUSHADY_HEAP_UPLOAD:
		heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;
		state = D3D12_RESOURCE_STATE_GENERIC_READ;
		break;
	case COMPUSHADY_HEAP_READBACK:
		heap_properties.Type = D3D12_HEAP_TYPE_READBACK;
		state = D3D12_RESOURCE_STATE_COPY_DEST;
		break;
	default:
		return PyErr_Format(PyExc_ValueError, "invalid heap type: %d", heap);
	}

	D3D12_RESOURCE_DESC1 resource_desc = {};
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resource_desc.Width = COMPUSHADY_ALIGN(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	resource_desc.Height = 1;
	resource_desc.DepthOrArraySize = 1;
	resource_desc.MipLevels = 1;
	resource_desc.Format = DXGI_FORMAT_UNKNOWN;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resource_desc.SampleDesc.Count = 1;
	if (heap_properties.Type == D3D12_HEAP_TYPE_DEFAULT)
	{
		resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}


	ID3D12Resource1* resource;
	HRESULT hr = py_device->device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC*)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);

	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_BufferError, hr, "Unable to create ID3D12Resource1");
	}

	d3d12_Resource* py_resource = (d3d12_Resource*)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		resource->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = resource;
	py_resource->heap_type = heap_properties.Type;
	py_resource->size = size;
	py_resource->dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	py_resource->stride = stride;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

static PyObject* d3d12_Device_create_texture1d(d3d12_Device* self, PyObject* args)
{
	UINT width;
	DXGI_FORMAT format;
	if (!PyArg_ParseTuple(args, "Ii", &width, &format))
		return NULL;

	if (dxgi_pixels_sizes.find(format) == dxgi_pixels_sizes.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC1 resource_desc = {};
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
	resource_desc.Width = width;
	resource_desc.Height = 1;
	resource_desc.DepthOrArraySize = 1;
	resource_desc.MipLevels = 1;
	resource_desc.Format = format;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resource_desc.SampleDesc.Count = 1;
	if (heap_properties.Type == D3D12_HEAP_TYPE_DEFAULT)
	{
		resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}

	ID3D12Resource1* resource;
	HRESULT hr = py_device->device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC*)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);

	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_Texture1DError, hr, "Unable to create ID3D12Resource1");
	}

	d3d12_Resource* py_resource = (d3d12_Resource*)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		resource->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = self;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = resource;
	py_device->device->GetCopyableFootprints((D3D12_RESOURCE_DESC*)&resource_desc, 0, 1, 0, &py_resource->footprint, NULL, NULL, &py_resource->size);
	py_resource->dimension = resource_desc.Dimension;
	py_resource->stride = 0;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

static PyObject* d3d12_Device_create_texture2d(d3d12_Device* self, PyObject* args)
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

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC1 resource_desc = {};
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resource_desc.Width = width;
	resource_desc.Height = height;
	resource_desc.DepthOrArraySize = 1;
	resource_desc.MipLevels = 1;
	resource_desc.Format = format;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resource_desc.SampleDesc.Count = 1;
	resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	ID3D12Resource1* resource;
	HRESULT hr = py_device->device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC*)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);

	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_Texture2DError, hr, "Unable to create ID3D12Resource1");
	}

	d3d12_Resource* py_resource = (d3d12_Resource*)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		resource->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = self;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = resource;
	py_device->device->GetCopyableFootprints((D3D12_RESOURCE_DESC*)&resource_desc, 0, 1, 0, &py_resource->footprint, NULL, NULL, &py_resource->size);
	py_resource->dimension = resource_desc.Dimension;
	py_resource->stride = 0;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

static PyObject* d3d12_Device_create_texture3d(d3d12_Device* self, PyObject* args)
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

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC1 resource_desc = {};
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	resource_desc.Width = width;
	resource_desc.Height = height;
	resource_desc.DepthOrArraySize = depth;
	resource_desc.MipLevels = 1;
	resource_desc.Format = format;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resource_desc.SampleDesc.Count = 1;
	resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	ID3D12Resource1* resource;
	HRESULT hr = py_device->device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC*)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);
	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_Texture3DError, hr, "Unable to create ID3D12Resource1");
	}

	d3d12_Resource* py_resource = (d3d12_Resource*)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		resource->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = self;
	Py_INCREF(py_resource->py_device);
	py_resource->resource = resource;

	py_device->device->GetCopyableFootprints((D3D12_RESOURCE_DESC*)&resource_desc, 0, 1, 0, &py_resource->footprint, NULL, NULL, &py_resource->size);
	py_resource->dimension = resource_desc.Dimension;
	py_resource->stride = 0;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

static PyObject* d3d12_Device_create_texture2d_from_native(d3d12_Device* self, PyObject* args)
{
	unsigned long long texture_ptr;
	uint32_t width;
	uint32_t height;
	DXGI_FORMAT format;
	if (!PyArg_ParseTuple(args, "KIIi", &texture_ptr, &width, &height, &format))
		return NULL;;

	ID3D12Resource1* resource = (ID3D12Resource1*)texture_ptr;
	D3D12_RESOURCE_DESC resource_desc = resource->GetDesc();
	if (resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
	{
		return PyErr_Format(PyExc_ValueError, "supplied resource has the wrong Dimension (expected: D3D12_RESOURCE_DIMENSION_TEXTURE2D) %d %d", resource_desc.Dimension, resource_desc.Width);
	}
	d3d12_Resource* py_resource = (d3d12_Resource*)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = self;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = resource;
	py_resource->resource->AddRef();
	py_resource->py_device->device->GetCopyableFootprints(&resource_desc, 0, 1, 0, &py_resource->footprint, NULL, NULL, &py_resource->size);
	py_resource->dimension = resource_desc.Dimension;
	py_resource->format = resource_desc.Format;

	return (PyObject*)py_resource;
}

static PyObject* d3d12_Device_get_debug_messages(d3d12_Device* self, PyObject* args)
{
	PyObject* py_list = PyList_New(0);

	ID3D12InfoQueue* queue;
	HRESULT hr = self->device->QueryInterface<ID3D12InfoQueue>(&queue);
	if (hr == S_OK)
	{
		UINT64 num_of_messages = queue->GetNumStoredMessages();
		for (UINT64 i = 0; i < num_of_messages; i++)
		{
			SIZE_T message_size = 0;
			queue->GetMessage(i, NULL, &message_size);
			D3D12_MESSAGE* message = (D3D12_MESSAGE*)PyMem_Malloc(message_size);
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

static PyObject* d3d12_Device_create_compute(d3d12_Device* self, PyObject* args, PyObject* kwds)
{
	const char* kwlist[] = { "shader", "cbv", "srv", "uav", NULL };
	Py_buffer view;
	PyObject* py_cbv = NULL;
	PyObject* py_srv = NULL;
	PyObject* py_uav = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*|OOO", (char**)kwlist,
		&view, &py_cbv, &py_srv, &py_uav))
		return NULL;

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	std::vector<d3d12_Resource*> cbv;
	std::vector<d3d12_Resource*> srv;
	std::vector<d3d12_Resource*> uav;
	if (!compushady_check_descriptors(&d3d12_Resource_Type, py_cbv, cbv, py_srv, srv, py_uav, uav))
	{
		PyBuffer_Release(&view);
		return NULL;
	}

	std::vector<D3D12_DESCRIPTOR_RANGE1> ranges;
	if (cbv.size() > 0)
	{
		D3D12_DESCRIPTOR_RANGE1 range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		range.NumDescriptors = (UINT)cbv.size();
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		ranges.push_back(range);
	}
	if (srv.size() > 0)
	{
		D3D12_DESCRIPTOR_RANGE1 range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = (UINT)srv.size();
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		ranges.push_back(range);
	}
	if (uav.size() > 0)
	{
		D3D12_DESCRIPTOR_RANGE1 range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		range.NumDescriptors = (UINT)uav.size();
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		ranges.push_back(range);
	}


	D3D12_ROOT_PARAMETER1 root_parameter = {};
	root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	root_parameter.DescriptorTable.NumDescriptorRanges = (UINT)ranges.size();
	root_parameter.DescriptorTable.pDescriptorRanges = ranges.data();

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_root_signature = {};
	versioned_root_signature.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	versioned_root_signature.Desc_1_1.NumParameters = 1;
	versioned_root_signature.Desc_1_1.pParameters = &root_parameter;

	ID3DBlob* serialized_root_signature;
	HRESULT hr = D3D12SerializeVersionedRootSignature(&versioned_root_signature, &serialized_root_signature, NULL);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to serialize Versioned Root Signature");
	}

	d3d12_Compute* py_compute = (d3d12_Compute*)PyObject_New(d3d12_Compute, &d3d12_Compute_Type);
	if (!py_compute)
	{
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Compute");
	}
	COMPUSHADY_CLEAR(py_compute);
	py_compute->py_device = py_device;
	Py_INCREF(py_compute->py_device);

	hr = py_device->device->CreateRootSignature(0, serialized_root_signature->GetBufferPointer(), serialized_root_signature->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&py_compute->root_signature);
	serialized_root_signature->Release();
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		Py_DECREF(py_compute);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Root Signature");
	}

	D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
	descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descriptor_heap_desc.NumDescriptors = (UINT)(cbv.size() + srv.size() + uav.size());

	hr = py_device->device->CreateDescriptorHeap(&descriptor_heap_desc, __uuidof(ID3D12DescriptorHeap), (void**)&py_compute->descriptor_heap);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		Py_DECREF(py_compute);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Descriptor Heap");
	}

	UINT increment = py_device->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = py_compute->descriptor_heap->GetCPUDescriptorHandleForHeapStart();

	for (d3d12_Resource* resource : cbv)
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
		cbv_desc.BufferLocation = resource->resource->GetGPUVirtualAddress();
		cbv_desc.SizeInBytes = (UINT)COMPUSHADY_ALIGN(resource->size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		py_device->device->CreateConstantBufferView(&cbv_desc, cpu_handle);
		cpu_handle.ptr += increment;
	}

	for (d3d12_Resource* resource : srv)
	{
		if (resource->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
			srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv_desc.Format = resource->format;
			srv_desc.Buffer.NumElements = (UINT)(resource->size / (srv_desc.Format ? dxgi_pixels_sizes[resource->format] : 1));
			srv_desc.Buffer.StructureByteStride = resource->stride;
			if (resource->stride > 0)
			{
				srv_desc.Buffer.NumElements = (UINT)(resource->size / resource->stride);
			}
			py_device->device->CreateShaderResourceView(resource->resource, &srv_desc, cpu_handle);
		}
		else
		{
			py_device->device->CreateShaderResourceView(resource->resource, NULL, cpu_handle);
		}
		cpu_handle.ptr += increment;
	}

	for (d3d12_Resource* resource : uav)
	{
		if (resource->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
			uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uav_desc.Format = resource->format;
			uav_desc.Buffer.NumElements = (UINT)(resource->size / (uav_desc.Format ? dxgi_pixels_sizes[resource->format] : 1));
			uav_desc.Buffer.StructureByteStride = resource->stride;
			if (resource->stride > 0)
			{
				uav_desc.Buffer.NumElements = (UINT)(resource->size / resource->stride);
			}
			py_device->device->CreateUnorderedAccessView(resource->resource, NULL, &uav_desc, cpu_handle);
		}
		else
		{
			py_device->device->CreateUnorderedAccessView(resource->resource, NULL, NULL, cpu_handle);
		}
		cpu_handle.ptr += increment;
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC compute_pipeline_desc = {};
	compute_pipeline_desc.pRootSignature = py_compute->root_signature;
	compute_pipeline_desc.CS.pShaderBytecode = view.buf;
	compute_pipeline_desc.CS.BytecodeLength = view.len;

	hr = py_device->device->CreateComputePipelineState(&compute_pipeline_desc, __uuidof(ID3D12PipelineState), (void**)&py_compute->pipeline);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		Py_DECREF(py_compute);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Compute Pipeline State");
	}

	PyBuffer_Release(&view);

	return (PyObject*)py_compute;
}

static PyObject* d3d12_Device_create_buffer_from_native(d3d12_Device* self, PyObject* args)
{
	unsigned long long texture_ptr;
	if (!PyArg_ParseTuple(args, "K", &texture_ptr))
		return NULL;

	ID3D12Resource1* resource = (ID3D12Resource1*)texture_ptr;
	D3D12_RESOURCE_DESC resource_desc = resource->GetDesc();
	if (resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		return PyErr_Format(PyExc_ValueError, "supplied resource has the wrong Dimension (expected: D3D12_RESOURCE_DIMENSION_BUFFER)");
	}
	d3d12_Resource* py_resource = (d3d12_Resource*)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = self;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = resource;
	py_resource->resource->AddRef();
	py_resource->py_device->device->GetCopyableFootprints(&resource_desc, 0, 1, 0, NULL, NULL, NULL, &py_resource->size);
	py_resource->dimension = resource_desc.Dimension;
	py_resource->stride = 0;

	return (PyObject*)py_resource;
}

static PyMethodDef d3d12_Device_methods[] = {
	{"create_buffer", (PyCFunction)d3d12_Device_create_buffer, METH_VARARGS, "Creates a Buffer object"},
	{"create_buffer_from_native", (PyCFunction)d3d12_Device_create_buffer_from_native, METH_VARARGS, "Creates a Buffer object from a low level pointer"},
	{"create_texture1d", (PyCFunction)d3d12_Device_create_texture1d, METH_VARARGS, "Creates a Texture1D object"},
	{"create_texture2d", (PyCFunction)d3d12_Device_create_texture2d, METH_VARARGS, "Creates a Texture2D object"},
	{"create_texture2d_from_native", (PyCFunction)d3d12_Device_create_texture2d_from_native, METH_VARARGS, "Creates a Texture2D object from a low level pointer"},
	{"create_texture3d", (PyCFunction)d3d12_Device_create_texture3d, METH_VARARGS, "Creates a Texture3D object"},
	{"get_debug_messages", (PyCFunction)d3d12_Device_get_debug_messages, METH_VARARGS, "Get Device's debug messages"},
	{"create_compute", (PyCFunction)d3d12_Device_create_compute, METH_VARARGS | METH_KEYWORDS, "Creates a Compute object"},
	{"create_swapchain", (PyCFunction)d3d12_Device_create_swapchain, METH_VARARGS, "Creates a Swapchain object"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* d3d12_Resource_upload(d3d12_Resource* self, PyObject* args)
{
	Py_buffer view;
	SIZE_T offset;
	if (!PyArg_ParseTuple(args, "y*K", &view, &offset))
		return NULL;

	if (offset + (SIZE_T)view.len > self->size)
	{
		size_t size = view.len;
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_ValueError, "supplied buffer is bigger than resource size: (offset %llu) %llu (expected no more than %llu)", offset, size, self->size);
	}

	char* mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void**)&mapped_data);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to Map() ID3D12Resource1");
	}

	memcpy(mapped_data + offset, view.buf, view.len);
	self->resource->Unmap(0, NULL);
	PyBuffer_Release(&view);

	Py_RETURN_NONE;
}

static PyObject* d3d12_Resource_upload2d(d3d12_Resource* self, PyObject* args)
{
	Py_buffer view;
	UINT pitch;
	UINT width;
	UINT height;
	UINT bytes_per_pixel;
	if (!PyArg_ParseTuple(args, "y*IIII", &view, &pitch, &width, &height, &bytes_per_pixel))
		return NULL;

	char* mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void**)&mapped_data);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to Map() ID3D12Resource1");
	}

	size_t offset = 0;
	size_t remains = view.len;
	size_t resource_remains = self->size;
	for (UINT y = 0; y < height; y++)
	{
		size_t amount = Py_MIN(width * bytes_per_pixel, Py_MIN(remains, resource_remains));
		memcpy(mapped_data + (pitch * y), (char*)view.buf + offset, amount);
		remains -= amount;
		if (remains == 0)
			break;
		resource_remains -= amount;
		offset += amount;
	}

	self->resource->Unmap(0, NULL);
	PyBuffer_Release(&view);

	Py_RETURN_NONE;
}

static PyObject* d3d12_Resource_readback(d3d12_Resource* self, PyObject* args)
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

	char* mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void**)&mapped_data);
	if (hr != S_OK)
	{
		return d3d_generate_exception(PyExc_Exception, hr, "Unable Map() ID3D12Resource1");
	}

	PyObject* py_bytes = PyBytes_FromStringAndSize(mapped_data + offset, size);
	self->resource->Unmap(0, NULL);
	return py_bytes;
}

static PyObject* d3d12_Resource_readback2d(d3d12_Resource* self, PyObject* args)
{
	UINT pitch;
	UINT width;
	UINT height;
	UINT bytes_per_pixel;
	if (!PyArg_ParseTuple(args, "IIII", &pitch, &width, &height, &bytes_per_pixel))
		return NULL;

	if (pitch * height > self->size)
	{
		return PyErr_Format(PyExc_ValueError, "requested buffer out of bounds: %llu (expected no more than %llu)", pitch * height, self->size);
	}

	char* mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void**)&mapped_data);
	if (hr != S_OK)
	{
		return d3d_generate_exception(PyExc_Exception, hr, "Unable Map() ID3D12Resource1");
	}

	char* data2d = (char*)PyMem_Malloc(width * height * bytes_per_pixel);
	if (!data2d)
	{
		self->resource->Unmap(0, NULL);
		return PyErr_Format(PyExc_MemoryError, "Unable to allocate memory for 2d data");
	}

	for (uint32_t y = 0; y < height; y++)
	{
		memcpy(data2d + (width * bytes_per_pixel * y), mapped_data + (pitch * y), width * bytes_per_pixel);
	}

	PyObject* py_bytes = PyBytes_FromStringAndSize(data2d, width * height * bytes_per_pixel);

	PyMem_Free(data2d);
	self->resource->Unmap(0, NULL);
	return py_bytes;
}

static PyObject* d3d12_Resource_readback_to_buffer(d3d12_Resource* self, PyObject* args)
{
	Py_buffer view;
	SIZE_T offset = 0;
	if (!PyArg_ParseTuple(args, "y*|K", &view, &offset))
		return NULL;

	if (offset > self->size)
	{
		return PyErr_Format(PyExc_ValueError, "requested buffer out of bounds: %llu (expected no more than %llu)", offset, self->size);
	}

	char* mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void**)&mapped_data);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable Map() ID3D12Resource1");
	}

	memcpy(view.buf, mapped_data + offset, Py_MIN((SIZE_T)view.len, self->size - offset));

	self->resource->Unmap(0, NULL);
	PyBuffer_Release(&view);
	Py_RETURN_NONE;
}

static PyObject* d3d12_Resource_copy_to(d3d12_Resource* self, PyObject* args)
{
	PyObject* py_destination;
	if (!PyArg_ParseTuple(args, "O", &py_destination))
		return NULL;

	int ret = PyObject_IsInstance(py_destination, (PyObject*)&d3d12_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}

	d3d12_Resource* dst_resource = (d3d12_Resource*)py_destination;
	SIZE_T dst_size = ((d3d12_Resource*)py_destination)->size;

	if (self->size > dst_size)
	{
		return PyErr_Format(PyExc_ValueError, "Resource size is bigger than destination size: %llu (expected no more than %llu)", self->size, dst_size);
	}

	D3D12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Transition.pResource = self->resource;
	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[1].Transition.pResource = dst_resource->resource;

	bool reset_barrier0 = false;
	bool reset_barrier1 = false;

	self->py_device->command_list->Reset(self->py_device->command_allocator, NULL);

	if (self->dimension == D3D12_RESOURCE_DIMENSION_BUFFER && dst_resource->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		if (self->heap_type == D3D12_HEAP_TYPE_DEFAULT)
		{
			barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
			self->py_device->command_list->ResourceBarrier(1, &barriers[0]);
			reset_barrier0 = true;
		}
		if (dst_resource->heap_type == D3D12_HEAP_TYPE_DEFAULT)
		{
			barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			self->py_device->command_list->ResourceBarrier(1, &barriers[1]);
			reset_barrier1 = true;
		}
		self->py_device->command_list->CopyBufferRegion(dst_resource->resource, 0, self->resource, 0, self->size);
	}
	else // texture copy
	{
		D3D12_TEXTURE_COPY_LOCATION dest_location = {};
		dest_location.pResource = dst_resource->resource;
		if (dst_resource->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		{
			dest_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			dest_location.PlacedFootprint = self->footprint;
		}
		else
		{
			barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			self->py_device->command_list->ResourceBarrier(1, &barriers[1]);
			dest_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			reset_barrier1 = true;
		}

		D3D12_TEXTURE_COPY_LOCATION src_location = {};
		src_location.pResource = self->resource;
		if (self->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		{
			src_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src_location.PlacedFootprint = dst_resource->footprint;
		}
		else
		{
			barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
			self->py_device->command_list->ResourceBarrier(1, &barriers[0]);
			src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			reset_barrier0 = true;
		}

		self->py_device->command_list->CopyTextureRegion(&dest_location, 0, 0, 0, &src_location, NULL);
	}

	if (reset_barrier0)
	{
		D3D12_RESOURCE_STATES tmp = barriers[0].Transition.StateBefore;
		barriers[0].Transition.StateBefore = barriers[0].Transition.StateAfter;
		barriers[0].Transition.StateAfter = tmp;
		self->py_device->command_list->ResourceBarrier(1, &barriers[0]);
	}

	if (reset_barrier1)
	{
		D3D12_RESOURCE_STATES tmp = barriers[1].Transition.StateBefore;
		barriers[1].Transition.StateBefore = barriers[1].Transition.StateAfter;
		barriers[1].Transition.StateAfter = tmp;
		self->py_device->command_list->ResourceBarrier(1, &barriers[1]);
	}

	self->py_device->command_list->Close();

	self->py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&self->py_device->command_list);
	self->py_device->queue->Signal(self->py_device->fence, ++self->py_device->fence_value);
	self->py_device->fence->SetEventOnCompletion(self->py_device->fence_value, self->py_device->fence_event);
	WaitForSingleObject(self->py_device->fence_event, INFINITE);

	Py_RETURN_NONE;
}

static PyMethodDef d3d12_Resource_methods[] = {
	{"upload", (PyCFunction)d3d12_Resource_upload, METH_VARARGS, "Upload bytes to a GPU Resource"},
	{"upload2d", (PyCFunction)d3d12_Resource_upload2d, METH_VARARGS, "Upload bytes to a GPU Resource given pitch, width, height and pixel size"},
	{"readback", (PyCFunction)d3d12_Resource_readback, METH_VARARGS, "Readback bytes from a GPU Resource"},
	{"readback_to_buffer", (PyCFunction)d3d12_Resource_readback, METH_VARARGS, "Readback into a buffer from a GPU Resource"},
	{"readback2d", (PyCFunction)d3d12_Resource_readback2d, METH_VARARGS, "Readback bytes from a GPU Resource given pitch, width, height and pixel size"},
	{"copy_to", (PyCFunction)d3d12_Resource_copy_to, METH_VARARGS, "Copy resource content to another resource"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* d3d12_Compute_dispatch(d3d12_Compute* self, PyObject* args)
{
	UINT x, y, z;
	if (!PyArg_ParseTuple(args, "III", &x, &y, &z))
		return NULL;

	self->py_device->command_list->Reset(self->py_device->command_allocator, self->pipeline);
	self->py_device->command_list->SetDescriptorHeaps(1, &self->descriptor_heap);
	self->py_device->command_list->SetComputeRootSignature(self->root_signature);
	self->py_device->command_list->SetComputeRootDescriptorTable(0, self->descriptor_heap->GetGPUDescriptorHandleForHeapStart());
	self->py_device->command_list->Dispatch(x, y, z);
	self->py_device->command_list->Close();

	self->py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&self->py_device->command_list);
	self->py_device->queue->Signal(self->py_device->fence, ++self->py_device->fence_value);
	self->py_device->fence->SetEventOnCompletion(self->py_device->fence_value, self->py_device->fence_event);
	WaitForSingleObject(self->py_device->fence_event, INFINITE);

	Py_RETURN_NONE;
}

static PyMethodDef d3d12_Compute_methods[] = {
	{"dispatch", (PyCFunction)d3d12_Compute_dispatch, METH_VARARGS, "Execute a Compute Pipeline"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* d3d12_get_discovered_devices(PyObject* self)
{
	IDXGIFactory1* factory = NULL;
	HRESULT hr = CreateDXGIFactory2(d3d12_debug ? DXGI_CREATE_FACTORY_DEBUG : 0, __uuidof(IDXGIFactory1), (void**)&factory);
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
			factory->Release();
			Py_DECREF(py_list);
			return d3d_generate_exception(PyExc_Exception, hr, "unable to call GetDesc1");
		}

		d3d12_Device* py_device = (d3d12_Device*)PyObject_New(d3d12_Device, &d3d12_Device_Type);
		if (!py_device)
		{
			factory->Release();
			Py_DECREF(py_list);
			return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Device");
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

static PyObject* d3d12_enable_debug(PyObject* self)
{
	ID3D12Debug* debug;
	D3D12GetDebugInterface(__uuidof(ID3D12Debug), (void**)&debug);
	debug->EnableDebugLayer();
	Py_RETURN_NONE;
}

static PyObject* d3d12_get_shader_binary_type(PyObject* self)
{
	return PyLong_FromLong(COMPUSHADY_SHADER_BINARY_TYPE_DXIL);
}

static PyMethodDef compushady_backends_d3d12_methods[] = {
	{"get_discovered_devices", (PyCFunction)d3d12_get_discovered_devices, METH_NOARGS, "Returns the list of discovered GPU devices"},
	{"enable_debug", (PyCFunction)d3d12_enable_debug, METH_NOARGS, "Enable GPU debug mode"},
	{"get_shader_binary_type", (PyCFunction)d3d12_get_shader_binary_type, METH_NOARGS, "Returns the required shader binary type"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static struct PyModuleDef compushady_backends_d3d12_module = {
	PyModuleDef_HEAD_INIT,
	"d3d12",
	NULL,
	-1,
	compushady_backends_d3d12_methods };

PyMODINIT_FUNC
PyInit_d3d12(void)
{
	PyObject* m = compushady_backend_init(
		&compushady_backends_d3d12_module,
		&d3d12_Device_Type, d3d12_Device_members, d3d12_Device_methods,
		&d3d12_Resource_Type, d3d12_Resource_members, d3d12_Resource_methods,
		&d3d12_Swapchain_Type, /*d3d12_Swapchain_members*/ NULL, d3d12_Swapchain_methods,
		&d3d12_Compute_Type, NULL, d3d12_Compute_methods
	);

	dxgi_init_pixel_formats();

	return m;
}
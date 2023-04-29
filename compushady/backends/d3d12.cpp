
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


typedef struct d3d12_Heap
{
	PyObject_HEAD;
	d3d12_Device* py_device;
	ID3D12Heap* heap;
	SIZE_T size;
	int heap_type;
} d3d12_Heap;

typedef struct d3d12_Resource
{
	PyObject_HEAD;
	d3d12_Device* py_device;
	ID3D12Resource1* resource;
	SIZE_T size;
	UINT stride;
	DXGI_FORMAT format;
	int heap_type;
	D3D12_RESOURCE_DIMENSION dimension;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
	d3d12_Heap* py_heap;
	SIZE_T requested_size;
} d3d12_Resource;

typedef struct d3d12_Swapchain
{
	PyObject_HEAD;
	d3d12_Device* py_device;
	IDXGISwapChain3* swapchain;
	DXGI_SWAP_CHAIN_DESC1 desc;
	std::vector<ID3D12Resource*> backbuffers;
} d3d12_Swapchain;

typedef struct d3d12_Sampler
{
	PyObject_HEAD;
	d3d12_Device* py_device;
	D3D12_SAMPLER_DESC sampler_desc;
} d3d12_Sampler;

typedef struct d3d12_Compute
{
	PyObject_HEAD;
	d3d12_Device* py_device;
	ID3D12RootSignature* root_signature;
	ID3D12DescriptorHeap* descriptor_heaps[2];
	ID3D12PipelineState* pipeline;
	ID3D12StateObject* state_object;
	ID3D12Resource1* raygen_table;
	ID3D12Resource1* hitgroup_table;
	ID3D12Resource1* miss_table;
	UINT num_heaps;
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
	{"heap_type", T_INT, offsetof(d3d12_Resource, heap_type), 0, "resource heap type"},
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

static ID3D12Resource1* d3d12_upload_buffer(ID3D12Device* device, void* data, const size_t size, const size_t alignment, const D3D12_RESOURCE_STATES state)
{

	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type = D3D12_HEAP_TYPE_UPLOAD;


	D3D12_RESOURCE_DESC1 resource_desc = {};
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resource_desc.Width = COMPUSHADY_ALIGN(size, alignment);
	resource_desc.Height = 1;
	resource_desc.DepthOrArraySize = 1;
	resource_desc.MipLevels = 1;
	resource_desc.Format = DXGI_FORMAT_UNKNOWN;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resource_desc.SampleDesc.Count = 1;

	ID3D12Resource1* resource = NULL;

	HRESULT hr = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC*)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);
	if (hr != S_OK)
	{
		return (ID3D12Resource1*)d3d_generate_exception(Compushady_BufferError, hr, "Unable to create ID3D12Resource1");
	}

	void* ptr = NULL;
	hr = resource->Map(0, NULL, &ptr);
	if (hr != S_OK)
	{
		resource->Release();
		return (ID3D12Resource1*)d3d_generate_exception(Compushady_BufferError, hr, "Unable to Map ID3D12Resource1");
	}

	memcpy(ptr, data, size);

	resource->Unmap(0, NULL);

	return resource;

}

static ID3D12Resource1* d3d12_uav_buffer(ID3D12Device* device, size_t size, const size_t alignment, const D3D12_RESOURCE_STATES state)
{
	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;


	D3D12_RESOURCE_DESC1 resource_desc = {};
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resource_desc.Width = COMPUSHADY_ALIGN(size, alignment);
	resource_desc.Height = 1;
	resource_desc.DepthOrArraySize = 1;
	resource_desc.MipLevels = 1;
	resource_desc.Format = DXGI_FORMAT_UNKNOWN;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resource_desc.SampleDesc.Count = 1;
	resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	ID3D12Resource1* resource = NULL;

	HRESULT hr = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC*)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);
	if (hr != S_OK)
	{
		return (ID3D12Resource1*)d3d_generate_exception(Compushady_BufferError, hr, "Unable to create ID3D12Resource1");
	}

	return resource;
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

	Py_XDECREF(self->py_heap);

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

static void d3d12_Heap_dealloc(d3d12_Heap* self)
{
	if (self->heap)
	{
		self->heap->Release();
	}

	Py_XDECREF(self->py_device);

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyMemberDef d3d12_Heap_members[] = {
	{ "size", T_ULONGLONG, offsetof(d3d12_Heap, size), 0, "heap size" },
	{ "heap_type", T_INT, offsetof(d3d12_Heap, heap_type), 0, "heap type" }, { NULL } /* Sentinel */
};

COMPUSHADY_NEW_TYPE(d3d12, Heap);

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
	if (self->descriptor_heaps[0])
		self->descriptor_heaps[0]->Release();
	if (self->descriptor_heaps[1])
		self->descriptor_heaps[1]->Release();
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

static void d3d12_Sampler_dealloc(d3d12_Sampler* self)
{
	Py_XDECREF(self->py_device);

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject d3d12_Sampler_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.d3d12.Sampler",  /* tp_name */
	sizeof(d3d12_Sampler),                                               /* tp_basicsize */
	0,																	 /* tp_itemsize */
	(destructor)d3d12_Sampler_dealloc,                                   /* tp_dealloc */
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
	"compushady d3d12 Sampler",                                          /* tp_doc */
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

	self->py_device->command_allocator->Reset();
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
	Py_BEGIN_ALLOW_THREADS;
	WaitForSingleObject(self->py_device->fence_event, INFINITE);
	Py_END_ALLOW_THREADS;

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
	uint32_t width = 0;
	uint32_t height = 0;

	if (!PyArg_ParseTuple(args, "KiI|ii", &window_handle, &format, &num_buffers))
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
	swap_chain_desc1.Width = width;
	swap_chain_desc1.Height = height;
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

static d3d12_Resource* d3d12_Device_create_resource(d3d12_Device* py_device, PyObject* py_exception, D3D12_RESOURCE_STATES state, D3D12_HEAP_PROPERTIES* heap_properties, D3D12_RESOURCE_DESC1* resource_desc, int heap_type, SIZE_T size, PyObject* py_heap, SIZE_T heap_offset)
{
	ID3D12Resource1* resource;
	HRESULT hr;

	bool has_heap = false;

	if (py_heap && py_heap != Py_None)
	{
		int ret = PyObject_IsInstance(py_heap, (PyObject*)&d3d12_Heap_Type);
		if (ret < 0)
		{
			return NULL;
		}
		else if (ret == 0)
		{
			return (d3d12_Resource*)PyErr_Format(PyExc_ValueError, "Expected a Heap object");
		}

		d3d12_Heap* py_d3d12_heap = (d3d12_Heap*)py_heap;
		if (py_d3d12_heap->py_device != py_device)
		{
			return (d3d12_Resource*)PyErr_Format(py_exception, "Cannot use heap from a different device");
		}
		if (py_d3d12_heap->heap_type != heap_type)
		{
			return (d3d12_Resource*)PyErr_Format(py_exception, "Unsupported heap type");
		}
		if (heap_offset + size > py_d3d12_heap->size)
		{
			return (d3d12_Resource*)PyErr_Format(py_exception,
				"supplied heap is not big enough for the resource size: (offset %llu) %llu "
				"(required %llu)",
				heap_offset, py_d3d12_heap->size, size);
		}
		hr = py_device->device->CreatePlacedResource(py_d3d12_heap->heap, heap_offset, (D3D12_RESOURCE_DESC*)resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);

		has_heap = true;
	}
	else
	{
		hr = py_device->device->CreateCommittedResource(heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC*)resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);
	}

	if (hr != S_OK)
	{
		return (d3d12_Resource*)d3d_generate_exception(py_exception, hr, "Unable to create ID3D12Resource1");
	}

	d3d12_Resource* py_resource = (d3d12_Resource*)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		resource->Release();
		return (d3d12_Resource*)PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);
	if (has_heap)
	{
		py_resource->py_heap = (d3d12_Heap*)py_heap;
		Py_INCREF(py_heap);
	}

	py_resource->resource = resource;
	py_resource->heap_type = heap_type;
	return py_resource;
}

static PyObject* d3d12_Device_create_heap(d3d12_Device* self, PyObject* args)
{
	int heap_type;
	SIZE_T size;

	if (!PyArg_ParseTuple(args, "iK", &heap_type, &size))
		return NULL;

	if (!size)
		return PyErr_Format(Compushady_HeapError, "zero size heap");

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D12_HEAP_DESC heap_desc = {};
	heap_desc.SizeInBytes = COMPUSHADY_ALIGN(size, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

	switch (heap_type)
	{
	case COMPUSHADY_HEAP_DEFAULT:
		heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
		break;
	case COMPUSHADY_HEAP_UPLOAD:
		heap_desc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;;
		break;
	case COMPUSHADY_HEAP_READBACK:
		heap_desc.Properties.Type = D3D12_HEAP_TYPE_READBACK;
		break;
	default:
		return PyErr_Format(Compushady_HeapError, "invalid heap type: %d", heap_type);
	}

	ID3D12Heap* heap = NULL;

	HRESULT hr = py_device->device->CreateHeap(&heap_desc, __uuidof(ID3D12Heap), (void**)&heap);

	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_HeapError, hr, "Unable to create ID3D12Heap");
	}

	d3d12_Heap* py_heap = COMPUSHADY_NEW(d3d12_Heap);
	if (!py_heap)
	{
		heap->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Heap");
	}
	COMPUSHADY_CLEAR(py_heap);
	py_heap->py_device = py_device;
	Py_INCREF(py_heap->py_device);

	py_heap->heap = heap;
	py_heap->heap_type = heap_type;
	py_heap->size = heap_desc.SizeInBytes;

	return (PyObject*)py_heap;
}


static PyObject* d3d12_Device_create_buffer(d3d12_Device* self, PyObject* args)
{
	int heap_type;
	SIZE_T size;
	UINT stride;
	DXGI_FORMAT format;
	PyObject* py_heap;
	SIZE_T heap_offset;
	if (!PyArg_ParseTuple(args, "iKIiOK", &heap_type, &size, &stride, &format, &py_heap, &heap_offset))
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
	switch (heap_type)
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
		return PyErr_Format(PyExc_ValueError, "invalid heap type: %d", heap_type);
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

	d3d12_Resource* py_resource = d3d12_Device_create_resource(py_device, Compushady_BufferError, state, &heap_properties, &resource_desc, heap_type, resource_desc.Width, py_heap, heap_offset);
	if (!py_resource)
	{
		return NULL;
	}

	py_resource->size = resource_desc.Width;
	py_resource->requested_size = size;
	py_resource->dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	py_resource->stride = stride;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

static PyObject* d3d12_Device_create_blas(d3d12_Device* self, PyObject* args)
{
	PyObject* py_vertex_buffer;
	PyObject* py_index_buffer;
	if (!PyArg_ParseTuple(args, "OO", &py_vertex_buffer, &py_index_buffer))
		return NULL;


	if (!PyObject_IsInstance(py_vertex_buffer, (PyObject*)&d3d12_Resource_Type))
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}

	d3d12_Resource* vertex_buffer_resource = (d3d12_Resource*)py_vertex_buffer;

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	ID3D12Device5* device5 = NULL;
	HRESULT hr = py_device->device->QueryInterface<ID3D12Device5>(&device5);
	if (hr != S_OK)
	{
		return PyErr_Format(PyExc_ValueError, "Raytracing is not supported on this Device");
	}

	ID3D12GraphicsCommandList4* command_list4 = NULL;
	hr = py_device->command_list->QueryInterface<ID3D12GraphicsCommandList4>(&command_list4);
	if (hr != S_OK)
	{
		return PyErr_Format(PyExc_ValueError, "Raytracing is not supported on this Device");
	}

	D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc = {};
	geometry_desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geometry_desc.Triangles.VertexBuffer.StartAddress = vertex_buffer_resource->resource->GetGPUVirtualAddress();
	geometry_desc.Triangles.VertexBuffer.StrideInBytes = vertex_buffer_resource->stride;
	geometry_desc.Triangles.VertexFormat = vertex_buffer_resource->format;
	printf("format %d\n", vertex_buffer_resource->format);
	geometry_desc.Triangles.VertexCount = (UINT)(vertex_buffer_resource->size / vertex_buffer_resource->stride);
	geometry_desc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
	geometry_desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_desc = {};
	blas_desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	blas_desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	blas_desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	blas_desc.Inputs.NumDescs = 1;
	blas_desc.Inputs.pGeometryDescs = &geometry_desc;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO as_brebuild_info = {};
	device5->GetRaytracingAccelerationStructurePrebuildInfo(&blas_desc.Inputs, &as_brebuild_info);


	ID3D12Resource1* scratch = d3d12_uav_buffer(py_device->device, as_brebuild_info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, D3D12_RESOURCE_STATE_COMMON);
	if (!scratch)
	{
		return NULL;
	}

	ID3D12Resource1* blas = d3d12_uav_buffer(py_device->device, as_brebuild_info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
	if (!blas)
	{
		scratch->Release();
		return NULL;
	}

	blas_desc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
	blas_desc.DestAccelerationStructureData = blas->GetGPUVirtualAddress();

	py_device->command_allocator->Reset();
	command_list4->Reset(py_device->command_allocator, NULL);

	command_list4->BuildRaytracingAccelerationStructure(&blas_desc, 0, NULL);
	command_list4->Close();

	py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&py_device->command_list);
	py_device->queue->Signal(py_device->fence, ++py_device->fence_value);
	py_device->fence->SetEventOnCompletion(py_device->fence_value, py_device->fence_event);

	Py_BEGIN_ALLOW_THREADS;
	WaitForSingleObject(py_device->fence_event, INFINITE);
	Py_END_ALLOW_THREADS;

	scratch->Release();

	d3d12_Resource* py_resource = (d3d12_Resource*)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		blas->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource (BLAS)");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	// by not settings size, we avoid unwanted copies or meaningless calls (read: this is basically a half-baked compushady buffer)
	py_resource->resource = blas;
	py_resource->heap_type = D3D12_HEAP_TYPE_DEFAULT;
	py_resource->dimension = D3D12_RESOURCE_DIMENSION_BUFFER;

	return (PyObject*)py_resource;
}

static PyObject* d3d12_Device_create_tlas(d3d12_Device* self, PyObject* args)
{
	PyObject* py_blas;
	if (!PyArg_ParseTuple(args, "O", &py_blas))
		return NULL;


	if (!PyObject_IsInstance(py_blas, (PyObject*)&d3d12_Resource_Type))
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}

	d3d12_Resource* blas = (d3d12_Resource*)py_blas;

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	ID3D12Device5* device5 = NULL;
	HRESULT hr = py_device->device->QueryInterface<ID3D12Device5>(&device5);
	if (hr != S_OK)
	{
		return PyErr_Format(PyExc_ValueError, "Raytracing is not supported on this Device");
	}

	ID3D12GraphicsCommandList4* command_list4 = NULL;
	hr = py_device->command_list->QueryInterface<ID3D12GraphicsCommandList4>(&command_list4);
	if (hr != S_OK)
	{
		return PyErr_Format(PyExc_ValueError, "Raytracing is not supported on this Device");
	}

	D3D12_RAYTRACING_INSTANCE_DESC instance_desc = {};
	instance_desc.AccelerationStructure = blas->resource->GetGPUVirtualAddress();
	instance_desc.InstanceMask = 1;
	instance_desc.Transform[0][0] = instance_desc.Transform[1][1] = instance_desc.Transform[2][2] = 1;


	ID3D12Resource* instances_buffer = d3d12_upload_buffer(py_device->device, &instance_desc, sizeof(instance_desc), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, D3D12_RESOURCE_STATE_COMMON);
	if (!instances_buffer)
	{
		return NULL;
	}

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_desc = {};
	tlas_desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	tlas_desc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	tlas_desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	tlas_desc.Inputs.NumDescs = 1;
	tlas_desc.Inputs.InstanceDescs = instances_buffer->GetGPUVirtualAddress();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO as_brebuild_info = {};
	device5->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_desc.Inputs, &as_brebuild_info);


	ID3D12Resource1* scratch = d3d12_uav_buffer(py_device->device, as_brebuild_info.ScratchDataSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, D3D12_RESOURCE_STATE_COMMON);
	if (!scratch)
	{
		instances_buffer->Release();
		return NULL;
	}

	ID3D12Resource1* tlas = d3d12_uav_buffer(py_device->device, as_brebuild_info.ResultDataMaxSizeInBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
	if (!tlas)
	{
		scratch->Release();
		instances_buffer->Release();
		return NULL;
	}

	tlas_desc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
	tlas_desc.DestAccelerationStructureData = tlas->GetGPUVirtualAddress();

	py_device->command_allocator->Reset();
	command_list4->Reset(py_device->command_allocator, NULL);

	command_list4->BuildRaytracingAccelerationStructure(&tlas_desc, 0, NULL);
	command_list4->Close();

	py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&command_list4);
	py_device->queue->Signal(py_device->fence, ++py_device->fence_value);
	py_device->fence->SetEventOnCompletion(py_device->fence_value, py_device->fence_event);


	Py_BEGIN_ALLOW_THREADS;
	WaitForSingleObject(py_device->fence_event, INFINITE);
	Py_END_ALLOW_THREADS;


	scratch->Release();
	instances_buffer->Release();

	d3d12_Resource* py_resource = (d3d12_Resource*)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		tlas->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource (TLAS)");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	// by not settings size, we avoid unwanted copies or meaningless calls (read: this is basically a half-baked compushady buffer)
	py_resource->resource = tlas;
	py_resource->heap_type = D3D12_HEAP_TYPE_DEFAULT;
	py_resource->dimension = D3D12_RESOURCE_DIMENSION_BUFFER;

	return (PyObject*)py_resource;
}

static PyObject* d3d12_Device_create_texture1d(d3d12_Device* self, PyObject* args)
{
	UINT width;
	DXGI_FORMAT format;
	PyObject* py_heap;
	SIZE_T heap_offset;
	if (!PyArg_ParseTuple(args, "IiOK", &width, &format, &py_heap, &heap_offset))
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
	resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	UINT64 texture_size = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	py_device->device->GetCopyableFootprints((D3D12_RESOURCE_DESC*)&resource_desc, 0, 1, 0, &footprint, NULL, NULL, &texture_size);


	d3d12_Resource* py_resource = d3d12_Device_create_resource(py_device, Compushady_Texture1DError, state, &heap_properties, &resource_desc, COMPUSHADY_HEAP_DEFAULT, texture_size, py_heap, heap_offset);
	if (!py_resource)
	{
		return NULL;
	}

	py_resource->size = texture_size;
	py_resource->requested_size = texture_size;
	py_resource->footprint = footprint;
	py_resource->dimension = resource_desc.Dimension;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

#define COMPUSHADY_D3D12_SAMPLER_ADDRESS_MODE(var, field) if (var == COMPUSHADY_SAMPLER_ADDRESS_MODE_WRAP)\
{\
	var = D3D12_TEXTURE_ADDRESS_MODE_WRAP;\
}\
else if (var == COMPUSHADY_SAMPLER_ADDRESS_MODE_MIRROR)\
{\
	var = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;\
}\
else if (var == COMPUSHADY_SAMPLER_ADDRESS_MODE_CLAMP)\
{\
	var = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;\
}\
else\
{\
	return PyErr_Format(Compushady_SamplerError, "unsupported address mode for " field);\
}

static PyObject* d3d12_Device_create_sampler(d3d12_Device* self, PyObject* args)
{
	int address_mode_u;
	int address_mode_v;
	int address_mode_w;
	int filter_min;
	int filter_mag;
	if (!PyArg_ParseTuple(args, "iiiii", &address_mode_u, &address_mode_v, &address_mode_w, &filter_min, &filter_mag))
		return NULL;


	COMPUSHADY_D3D12_SAMPLER_ADDRESS_MODE(address_mode_u, "U");
	COMPUSHADY_D3D12_SAMPLER_ADDRESS_MODE(address_mode_v, "V");
	COMPUSHADY_D3D12_SAMPLER_ADDRESS_MODE(address_mode_w, "W");

	D3D12_FILTER filter = D3D12_FILTER_MIN_MAG_MIP_POINT;

	if (filter_min == COMPUSHADY_SAMPLER_FILTER_POINT && filter_mag == COMPUSHADY_SAMPLER_FILTER_POINT)
	{
		filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	}
	else if (filter_min == COMPUSHADY_SAMPLER_FILTER_LINEAR && filter_mag == COMPUSHADY_SAMPLER_FILTER_POINT)
	{
		filter = D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
	}
	else if (filter_min == COMPUSHADY_SAMPLER_FILTER_POINT && filter_mag == COMPUSHADY_SAMPLER_FILTER_LINEAR)
	{
		filter = D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
	}
	else if (filter_min == COMPUSHADY_SAMPLER_FILTER_LINEAR && filter_mag == COMPUSHADY_SAMPLER_FILTER_LINEAR)
	{
		filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	}
	else
	{
		return PyErr_Format(Compushady_SamplerError, "unsupported filter");
	}

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;


	d3d12_Sampler* py_sampler = (d3d12_Sampler*)PyObject_New(d3d12_Sampler, &d3d12_Sampler_Type);
	if (!py_sampler)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Sampler");
	}
	COMPUSHADY_CLEAR(py_sampler);
	py_sampler->py_device = self;
	Py_INCREF(py_sampler->py_device);

	py_sampler->sampler_desc.AddressU = (D3D12_TEXTURE_ADDRESS_MODE)address_mode_u;
	py_sampler->sampler_desc.AddressV = (D3D12_TEXTURE_ADDRESS_MODE)address_mode_v;
	py_sampler->sampler_desc.AddressW = (D3D12_TEXTURE_ADDRESS_MODE)address_mode_w;
	py_sampler->sampler_desc.Filter = filter;

	return (PyObject*)py_sampler;
}

static PyObject* d3d12_Device_create_texture2d(d3d12_Device* self, PyObject* args)
{
	UINT width;
	UINT height;
	DXGI_FORMAT format;
	PyObject* py_heap;
	SIZE_T heap_offset;
	if (!PyArg_ParseTuple(args, "IIiOK", &width, &height, &format, &py_heap, &heap_offset))
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

	UINT64 texture_size = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	py_device->device->GetCopyableFootprints((D3D12_RESOURCE_DESC*)&resource_desc, 0, 1, 0, &footprint, NULL, NULL, &texture_size);


	d3d12_Resource* py_resource = d3d12_Device_create_resource(py_device, Compushady_Texture2DError, state, &heap_properties, &resource_desc, COMPUSHADY_HEAP_DEFAULT, texture_size, py_heap, heap_offset);
	if (!py_resource)
	{
		return NULL;
	}

	py_resource->size = texture_size;
	py_resource->requested_size = texture_size;
	py_resource->footprint = footprint;
	py_resource->dimension = resource_desc.Dimension;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

static PyObject* d3d12_Device_create_texture3d(d3d12_Device* self, PyObject* args)
{
	UINT width;
	UINT height;
	UINT depth;
	DXGI_FORMAT format;
	PyObject* py_heap;
	SIZE_T heap_offset;
	if (!PyArg_ParseTuple(args, "IIIiOK", &width, &height, &depth, &format, &py_heap, &heap_offset))
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

	UINT64 texture_size = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	py_device->device->GetCopyableFootprints((D3D12_RESOURCE_DESC*)&resource_desc, 0, 1, 0, &footprint, NULL, NULL, &texture_size);


	d3d12_Resource* py_resource = d3d12_Device_create_resource(py_device, Compushady_Texture3DError, state, &heap_properties, &resource_desc, COMPUSHADY_HEAP_DEFAULT, texture_size, py_heap, heap_offset);
	if (!py_resource)
	{
		return NULL;
	}

	py_resource->size = texture_size;
	py_resource->requested_size = texture_size;
	py_resource->footprint = footprint;
	py_resource->dimension = resource_desc.Dimension;
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
	const char* kwlist[] = { "shader", "cbv", "srv", "uav", "samplers", NULL };
	Py_buffer view;
	PyObject* py_cbv = NULL;
	PyObject* py_srv = NULL;
	PyObject* py_uav = NULL;
	PyObject* py_samplers = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*|OOOO", (char**)kwlist,
		&view, &py_cbv, &py_srv, &py_uav, &py_samplers))
		return NULL;

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	std::vector<d3d12_Resource*> cbv;
	std::vector<d3d12_Resource*> srv;
	std::vector<d3d12_Resource*> uav;
	std::vector<d3d12_Sampler*> samplers;

	if (!compushady_check_descriptors(&d3d12_Resource_Type, py_cbv, cbv, py_srv, srv, py_uav, uav, &d3d12_Sampler_Type, py_samplers, samplers))
	{
		PyBuffer_Release(&view);
		return NULL;
	}

	std::vector<D3D12_DESCRIPTOR_RANGE1> ranges;
	std::vector<D3D12_DESCRIPTOR_RANGE1> samplers_ranges;

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
	if (samplers.size() > 0)
	{
		D3D12_DESCRIPTOR_RANGE1 range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		range.NumDescriptors = (UINT)samplers.size();
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		samplers_ranges.push_back(range);
	}

	UINT num_params = 0;

	D3D12_ROOT_PARAMETER1 root_parameters[2] = {};

	if (ranges.size() > 0)
	{
		root_parameters[num_params].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		root_parameters[num_params].DescriptorTable.NumDescriptorRanges = (UINT)ranges.size();
		root_parameters[num_params].DescriptorTable.pDescriptorRanges = ranges.data();
		num_params++;
	}

	if (samplers_ranges.size() > 0)
	{
		root_parameters[num_params].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		root_parameters[num_params].DescriptorTable.NumDescriptorRanges = (UINT)samplers_ranges.size();
		root_parameters[num_params].DescriptorTable.pDescriptorRanges = samplers_ranges.data();
		num_params++;
	}

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_root_signature = {};
	versioned_root_signature.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	versioned_root_signature.Desc_1_1.NumParameters = num_params;
	versioned_root_signature.Desc_1_1.pParameters = root_parameters;

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

	UINT num_resource_descriptors = (UINT)(cbv.size() + srv.size() + uav.size());

	if (num_resource_descriptors > 0)
	{

		D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
		descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		descriptor_heap_desc.NumDescriptors = num_resource_descriptors;

		hr = py_device->device->CreateDescriptorHeap(&descriptor_heap_desc, __uuidof(ID3D12DescriptorHeap), (void**)&py_compute->descriptor_heaps[py_compute->num_heaps]);
		if (hr != S_OK)
		{
			PyBuffer_Release(&view);
			Py_DECREF(py_compute);
			return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Descriptor Heap");
		}

		UINT increment = py_device->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = py_compute->descriptor_heaps[py_compute->num_heaps]->GetCPUDescriptorHandleForHeapStart();

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

		py_compute->num_heaps++;
	}

	if (samplers.size() > 0)
	{

		D3D12_DESCRIPTOR_HEAP_DESC sampler_descriptor_heap_desc = {};
		sampler_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		sampler_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		sampler_descriptor_heap_desc.NumDescriptors = (UINT)samplers.size();

		hr = py_device->device->CreateDescriptorHeap(&sampler_descriptor_heap_desc, __uuidof(ID3D12DescriptorHeap), (void**)&py_compute->descriptor_heaps[py_compute->num_heaps]);
		if (hr != S_OK)
		{
			PyBuffer_Release(&view);
			Py_DECREF(py_compute);
			return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Sampler Descriptor Heap");
		}

		UINT sampler_increment = py_device->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_handle = py_compute->descriptor_heaps[py_compute->num_heaps]->GetCPUDescriptorHandleForHeapStart();

		for (d3d12_Sampler* sampler : samplers)
		{
			py_device->device->CreateSampler(&sampler->sampler_desc, sampler_cpu_handle);
			sampler_cpu_handle.ptr += sampler_increment;
		}


		py_compute->num_heaps++;
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

static PyObject* d3d12_Device_create_raytracer(d3d12_Device* self, PyObject* args, PyObject* kwds)
{
	const char* kwlist[] = { "shader", "cbv", "srv", "uav", "samplers", NULL };
	Py_buffer view;
	PyObject* py_cbv = NULL;
	PyObject* py_srv = NULL;
	PyObject* py_uav = NULL;
	PyObject* py_samplers = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*|OOOO", (char**)kwlist,
		&view, &py_cbv, &py_srv, &py_uav, &py_samplers))
		return NULL;

	d3d12_Device* py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	ID3D12Device5* device5 = NULL;
	HRESULT hr = py_device->device->QueryInterface<ID3D12Device5>(&device5);
	if (hr != S_OK)
	{
		return PyErr_Format(PyExc_ValueError, "Raytracing is not supported on this Device");
	}

	std::vector<d3d12_Resource*> cbv;
	std::vector<d3d12_Resource*> srv;
	std::vector<d3d12_Resource*> uav;
	std::vector<d3d12_Sampler*> samplers;

	if (!compushady_check_descriptors(&d3d12_Resource_Type, py_cbv, cbv, py_srv, srv, py_uav, uav, &d3d12_Sampler_Type, py_samplers, samplers))
	{
		PyBuffer_Release(&view);
		return NULL;
	}

	std::vector<D3D12_DESCRIPTOR_RANGE1> ranges;
	std::vector<D3D12_DESCRIPTOR_RANGE1> samplers_ranges;

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
	if (samplers.size() > 0)
	{
		D3D12_DESCRIPTOR_RANGE1 range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		range.NumDescriptors = (UINT)samplers.size();
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		samplers_ranges.push_back(range);
	}

	UINT num_params = 0;

	D3D12_ROOT_PARAMETER1 root_parameters[2] = {};

	if (ranges.size() > 0)
	{
		root_parameters[num_params].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		root_parameters[num_params].DescriptorTable.NumDescriptorRanges = (UINT)ranges.size();
		root_parameters[num_params].DescriptorTable.pDescriptorRanges = ranges.data();
		num_params++;
	}

	if (samplers_ranges.size() > 0)
	{
		root_parameters[num_params].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		root_parameters[num_params].DescriptorTable.NumDescriptorRanges = (UINT)samplers_ranges.size();
		root_parameters[num_params].DescriptorTable.pDescriptorRanges = samplers_ranges.data();
		num_params++;
	}

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_root_signature = {};
	versioned_root_signature.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	versioned_root_signature.Desc_1_1.NumParameters = num_params;
	versioned_root_signature.Desc_1_1.pParameters = root_parameters;

	ID3DBlob* serialized_root_signature;
	hr = D3D12SerializeVersionedRootSignature(&versioned_root_signature, &serialized_root_signature, NULL);
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

	UINT num_resource_descriptors = (UINT)(cbv.size() + srv.size() + uav.size());

	if (num_resource_descriptors > 0)
	{

		D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
		descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		descriptor_heap_desc.NumDescriptors = num_resource_descriptors;

		hr = py_device->device->CreateDescriptorHeap(&descriptor_heap_desc, __uuidof(ID3D12DescriptorHeap), (void**)&py_compute->descriptor_heaps[py_compute->num_heaps]);
		if (hr != S_OK)
		{
			PyBuffer_Release(&view);
			Py_DECREF(py_compute);
			return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Descriptor Heap");
		}

		UINT increment = py_device->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = py_compute->descriptor_heaps[py_compute->num_heaps]->GetCPUDescriptorHandleForHeapStart();

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
				srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
				srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srv_desc.RaytracingAccelerationStructure.Location = resource->resource->GetGPUVirtualAddress();
				/*srv_desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
				srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srv_desc.Format = resource->format;
				srv_desc.Buffer.NumElements = (UINT)(resource->size / (srv_desc.Format ? dxgi_pixels_sizes[resource->format] : 1));
				srv_desc.Buffer.StructureByteStride = resource->stride;
				if (resource->stride > 0)
				{
					srv_desc.Buffer.NumElements = (UINT)(resource->size / resource->stride);
				}*/
				py_device->device->CreateShaderResourceView(NULL, &srv_desc, cpu_handle);
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

		py_compute->num_heaps++;
	}

	if (samplers.size() > 0)
	{

		D3D12_DESCRIPTOR_HEAP_DESC sampler_descriptor_heap_desc = {};
		sampler_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		sampler_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		sampler_descriptor_heap_desc.NumDescriptors = (UINT)samplers.size();

		hr = py_device->device->CreateDescriptorHeap(&sampler_descriptor_heap_desc, __uuidof(ID3D12DescriptorHeap), (void**)&py_compute->descriptor_heaps[py_compute->num_heaps]);
		if (hr != S_OK)
		{
			PyBuffer_Release(&view);
			Py_DECREF(py_compute);
			return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Sampler Descriptor Heap");
		}

		UINT sampler_increment = py_device->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_handle = py_compute->descriptor_heaps[py_compute->num_heaps]->GetCPUDescriptorHandleForHeapStart();

		for (d3d12_Sampler* sampler : samplers)
		{
			py_device->device->CreateSampler(&sampler->sampler_desc, sampler_cpu_handle);
			sampler_cpu_handle.ptr += sampler_increment;
		}


		py_compute->num_heaps++;
	}

	D3D12_DXIL_LIBRARY_DESC dxil_library_desc = {};
	dxil_library_desc.DXILLibrary.pShaderBytecode = view.buf;
	dxil_library_desc.DXILLibrary.BytecodeLength = view.len;

	D3D12_GLOBAL_ROOT_SIGNATURE global_root_signature = {};
	global_root_signature.pGlobalRootSignature = py_compute->root_signature;

	D3D12_RAYTRACING_PIPELINE_CONFIG raytracing_pipeline_config = {};
	raytracing_pipeline_config.MaxTraceRecursionDepth = 1;

	D3D12_RAYTRACING_SHADER_CONFIG raytracing_shader_config = {};
	raytracing_shader_config.MaxPayloadSizeInBytes = sizeof(float) * 4;
	raytracing_shader_config.MaxAttributeSizeInBytes = sizeof(float) * 2;

	D3D12_HIT_GROUP_DESC hit_group_desc = {};
	hit_group_desc.HitGroupExport = L"hitgroup";
	hit_group_desc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
	hit_group_desc.ClosestHitShaderImport = L"main2";


	D3D12_STATE_SUBOBJECT state_subobjects[5] = {};
	state_subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	state_subobjects[0].pDesc = &dxil_library_desc;

	state_subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
	state_subobjects[1].pDesc = &global_root_signature;

	state_subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
	state_subobjects[2].pDesc = &raytracing_pipeline_config;

	state_subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
	state_subobjects[3].pDesc = &raytracing_shader_config;

	state_subobjects[4].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
	state_subobjects[4].pDesc = &hit_group_desc;


	D3D12_STATE_OBJECT_DESC state_object_desc = {};
	state_object_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	state_object_desc.NumSubobjects = 5;
	state_object_desc.pSubobjects = state_subobjects;

	printf("OKOK!\n");


	hr = device5->CreateStateObject(&state_object_desc, __uuidof(ID3D12StateObject), (void**)&py_compute->state_object);

	printf("DONE!\n");
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		Py_DECREF(py_compute);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to create RayTracing Pipeline State");
	}

	PyBuffer_Release(&view);

	ID3D12StateObjectProperties* state_object_props = NULL;

	hr = py_compute->state_object->QueryInterface<ID3D12StateObjectProperties>(&state_object_props);
	if (hr != S_OK)
	{
		Py_DECREF(py_compute);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to create RayTracing Pipeline State");
	}

	py_compute->raygen_table = d3d12_upload_buffer(self->device, state_object_props->GetShaderIdentifier(L"main"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, D3D12_RESOURCE_STATE_COMMON);
	py_compute->hitgroup_table = d3d12_upload_buffer(self->device, state_object_props->GetShaderIdentifier(L"hitgroup"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, D3D12_RESOURCE_STATE_COMMON);
	py_compute->miss_table = d3d12_upload_buffer(self->device, state_object_props->GetShaderIdentifier(L"main3"), D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, D3D12_RESOURCE_STATE_COMMON);


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
	{"create_raytracer", (PyCFunction)d3d12_Device_create_raytracer, METH_VARARGS | METH_KEYWORDS, "Creates a RayTracer object"},
	{"create_swapchain", (PyCFunction)d3d12_Device_create_swapchain, METH_VARARGS, "Creates a Swapchain object"},
	{"create_sampler", (PyCFunction)d3d12_Device_create_sampler, METH_VARARGS, "Creates a Sampler object"},
	{"create_blas", (PyCFunction)d3d12_Device_create_blas, METH_VARARGS, "Creates Bottom Level Acceleration Structure object"},
	{"create_tlas", (PyCFunction)d3d12_Device_create_tlas, METH_VARARGS, "Creates Top Level Acceleration Structure object"},
	{"create_heap", (PyCFunction)d3d12_Device_create_heap, METH_VARARGS, "Creates a Heap object"},
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

static PyObject* d3d12_Resource_upload_chunked(d3d12_Resource* self, PyObject* args)
{
	Py_buffer view;
	UINT stride;
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

	char* mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void**)&mapped_data);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		PyBuffer_Release(&filler);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to Map() ID3D12Resource1");
	}

	size_t offset = 0;
	for (uint32_t i = 0; i < elements; i++)
	{
		memcpy(mapped_data + offset, (char*)view.buf + (i * stride), stride);
		offset += stride;
		memcpy(mapped_data + offset, (char*)filler.buf, filler.len);
		offset += filler.len;
	}

	self->resource->Unmap(0, NULL);
	PyBuffer_Release(&view);
	PyBuffer_Release(&filler);
	Py_RETURN_NONE;
}

static PyObject* d3d12_Resource_readback(d3d12_Resource* self, PyObject* args)
{
	SIZE_T size;
	SIZE_T offset;
	if (!PyArg_ParseTuple(args, "KK", &size, &offset))
		return NULL;

	// use the requested size
	if (size == 0)
		size = self->requested_size - offset;

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
	if (!PyArg_ParseTuple(args, "y*K", &view, &offset))
		return NULL;

	if (offset > self->size)
	{
		PyBuffer_Release(&view);
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
	SIZE_T dst_size = ((d3d12_Resource*)py_destination)->requested_size;

	if (self->requested_size > dst_size)
	{
		return PyErr_Format(PyExc_ValueError, "Resource size is bigger than destination size: %llu (expected no more than %llu)", self->size, dst_size);
	}

	if (self->py_device != dst_resource->py_device)
	{
		return PyErr_Format(Compushady_DeviceError, "Cannot copy between devices");
	}

	D3D12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Transition.pResource = self->resource;
	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[1].Transition.pResource = dst_resource->resource;

	bool reset_barrier0 = false;
	bool reset_barrier1 = false;

	self->py_device->command_allocator->Reset();
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
		self->py_device->command_list->CopyBufferRegion(dst_resource->resource, 0, self->resource, 0, self->requested_size);
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
	Py_BEGIN_ALLOW_THREADS;
	WaitForSingleObject(self->py_device->fence_event, INFINITE);
	Py_END_ALLOW_THREADS;

	Py_RETURN_NONE;
}

static PyMethodDef d3d12_Resource_methods[] = {
	{"upload", (PyCFunction)d3d12_Resource_upload, METH_VARARGS, "Upload bytes to a GPU Resource"},
	{"upload2d", (PyCFunction)d3d12_Resource_upload2d, METH_VARARGS, "Upload bytes to a GPU Resource given pitch, width, height and pixel size"},
	{"upload_chunked", (PyCFunction)d3d12_Resource_upload_chunked, METH_VARARGS, "Upload bytes to a GPU Resource with the given stride and a filler"},
	{"readback", (PyCFunction)d3d12_Resource_readback, METH_VARARGS, "Readback bytes from a GPU Resource"},
	{"readback_to_buffer", (PyCFunction)d3d12_Resource_readback_to_buffer, METH_VARARGS, "Readback into a buffer from a GPU Resource"},
	{"readback2d", (PyCFunction)d3d12_Resource_readback2d, METH_VARARGS, "Readback bytes from a GPU Resource given pitch, width, height and pixel size"},
	{"copy_to", (PyCFunction)d3d12_Resource_copy_to, METH_VARARGS, "Copy resource content to another resource"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* d3d12_Compute_dispatch(d3d12_Compute* self, PyObject* args)
{
	UINT x, y, z;
	if (!PyArg_ParseTuple(args, "III", &x, &y, &z))
		return NULL;

	self->py_device->command_allocator->Reset();
	self->py_device->command_list->Reset(self->py_device->command_allocator, self->pipeline);
	self->py_device->command_list->SetDescriptorHeaps(self->num_heaps, self->descriptor_heaps);
	self->py_device->command_list->SetComputeRootSignature(self->root_signature);
	if (self->num_heaps > 0)
	{
		self->py_device->command_list->SetComputeRootDescriptorTable(0, self->descriptor_heaps[0]->GetGPUDescriptorHandleForHeapStart());
		if (self->num_heaps > 1)
		{
			self->py_device->command_list->SetComputeRootDescriptorTable(1, self->descriptor_heaps[1]->GetGPUDescriptorHandleForHeapStart());
		}
	}
	self->py_device->command_list->Dispatch(x, y, z);
	self->py_device->command_list->Close();

	self->py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&self->py_device->command_list);
	self->py_device->queue->Signal(self->py_device->fence, ++self->py_device->fence_value);
	self->py_device->fence->SetEventOnCompletion(self->py_device->fence_value, self->py_device->fence_event);
	Py_BEGIN_ALLOW_THREADS;
	WaitForSingleObject(self->py_device->fence_event, INFINITE);
	Py_END_ALLOW_THREADS;

	Py_RETURN_NONE;
}

static PyObject* d3d12_Compute_dispatch_rays(d3d12_Compute* self, PyObject* args)
{
	UINT x, y, z;
	if (!PyArg_ParseTuple(args, "III", &x, &y, &z))
		return NULL;

	ID3D12GraphicsCommandList4* command_list4 = NULL;
	HRESULT hr = self->py_device->command_list->QueryInterface<ID3D12GraphicsCommandList4>(&command_list4);
	if (hr != S_OK)
	{
		return PyErr_Format(PyExc_ValueError, "Raytracing is not supported on this Device");
	}

	D3D12_DISPATCH_RAYS_DESC dispatch_rays_desc = {};
	dispatch_rays_desc.Width = x;
	dispatch_rays_desc.Height = y;
	dispatch_rays_desc.Depth = z;

	dispatch_rays_desc.RayGenerationShaderRecord.StartAddress = self->raygen_table->GetGPUVirtualAddress();
	dispatch_rays_desc.RayGenerationShaderRecord.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

	dispatch_rays_desc.HitGroupTable.StartAddress = self->hitgroup_table->GetGPUVirtualAddress();
	dispatch_rays_desc.HitGroupTable.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	dispatch_rays_desc.HitGroupTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

	dispatch_rays_desc.MissShaderTable.StartAddress = self->miss_table->GetGPUVirtualAddress();
	dispatch_rays_desc.MissShaderTable.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	dispatch_rays_desc.MissShaderTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

	self->py_device->command_allocator->Reset();
	command_list4->Reset(self->py_device->command_allocator, NULL);
	command_list4->SetDescriptorHeaps(self->num_heaps, self->descriptor_heaps);
	command_list4->SetComputeRootSignature(self->root_signature);
	if (self->num_heaps > 0)
	{
		command_list4->SetComputeRootDescriptorTable(0, self->descriptor_heaps[0]->GetGPUDescriptorHandleForHeapStart());
		if (self->num_heaps > 1)
		{
			command_list4->SetComputeRootDescriptorTable(1, self->descriptor_heaps[1]->GetGPUDescriptorHandleForHeapStart());
		}
	}

	command_list4->SetPipelineState1(self->state_object);
	command_list4->DispatchRays(&dispatch_rays_desc);
	command_list4->Close();

	self->py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&command_list4);
	self->py_device->queue->Signal(self->py_device->fence, ++self->py_device->fence_value);
	self->py_device->fence->SetEventOnCompletion(self->py_device->fence_value, self->py_device->fence_event);
	Py_BEGIN_ALLOW_THREADS;
	WaitForSingleObject(self->py_device->fence_event, INFINITE);
	Py_END_ALLOW_THREADS;

	Py_RETURN_NONE;
}

static PyMethodDef d3d12_Compute_methods[] = {
	{"dispatch", (PyCFunction)d3d12_Compute_dispatch, METH_VARARGS, "Execute a Compute Pipeline"},
	{"dispatch_rays", (PyCFunction)d3d12_Compute_dispatch_rays, METH_VARARGS, "Execute a RayTracer Pipeline"},
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

static compushady_backend_desc_t d3d12_backend;

PyMODINIT_FUNC
PyInit_d3d12(void)
{
	compushady_backend_desc_init(&d3d12_backend, "d3d12", compushady_backends_d3d12_methods);

	d3d12_backend.device_type = &d3d12_Device_Type;
	d3d12_backend.device_members = d3d12_Device_members;
	d3d12_backend.device_methods = d3d12_Device_methods;

	d3d12_backend.resource_type = &d3d12_Resource_Type;
	d3d12_backend.resource_members = d3d12_Resource_members;
	d3d12_backend.resource_methods = d3d12_Resource_methods;

	d3d12_backend.swapchain_type = &d3d12_Swapchain_Type;
	//d3d12_backend.swapchain_members = d3d12_Swapchain_members;
	d3d12_backend.swapchain_methods = d3d12_Swapchain_methods;

	d3d12_backend.compute_type = &d3d12_Compute_Type;
	d3d12_backend.compute_methods = d3d12_Compute_methods;

	d3d12_backend.sampler_type = &d3d12_Sampler_Type;

	d3d12_backend.heap_type = &d3d12_Heap_Type;
	d3d12_backend.heap_members = d3d12_Heap_members;

	PyObject* m = compushady_backend_init(&d3d12_backend);
	if (!m)
		return NULL;

	dxgi_init_pixel_formats();

	return m;
}
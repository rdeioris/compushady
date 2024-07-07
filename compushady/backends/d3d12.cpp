
#include <d3d12.h>
#include <dxgi1_4.h>

#include "compushady.h"

void dxgi_init_pixel_formats();
PyObject *d3d_generate_exception(PyObject *py_exc, HRESULT hr, const char *prefix);
extern std::unordered_map<int, size_t> dxgi_pixels_sizes;

static bool d3d12_debug = false;

typedef struct d3d12_Device
{
	PyObject_HEAD;
	IDXGIAdapter1 *adapter;
	ID3D12Device1 *device;
	ID3D12CommandQueue *queue;
	ID3D12Fence1 *fence;
	HANDLE fence_event;
	UINT64 fence_value;
	ID3D12CommandAllocator *command_allocator;
	ID3D12GraphicsCommandList1 *command_list;
	PyObject *name;
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
	d3d12_Device *py_device;
	ID3D12Heap *heap;
	SIZE_T size;
	int heap_type;
} d3d12_Heap;

typedef struct d3d12_Resource
{
	PyObject_HEAD;
	d3d12_Device *py_device;
	ID3D12Resource1 *resource;
	SIZE_T size;
	UINT stride;
	DXGI_FORMAT format;
	int heap_type;
	D3D12_RESOURCE_DIMENSION dimension;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
	d3d12_Heap *py_heap;
	UINT slices;
	SIZE_T heap_size;
	UINT tiles_x;
	UINT tiles_y;
	UINT tiles_z;
	UINT tile_width;
	UINT tile_height;
	UINT tile_depth;
} d3d12_Resource;

typedef struct d3d12_Swapchain
{
	PyObject_HEAD;
	d3d12_Device *py_device;
	IDXGISwapChain3 *swapchain;
	DXGI_SWAP_CHAIN_DESC1 desc;
	std::vector<ID3D12Resource *> backbuffers;
} d3d12_Swapchain;

typedef struct d3d12_Sampler
{
	PyObject_HEAD;
	d3d12_Device *py_device;
	D3D12_SAMPLER_DESC sampler_desc;
} d3d12_Sampler;

typedef struct d3d12_Pipeline
{
	PyObject_HEAD;
	d3d12_Device *py_device;
	ID3D12RootSignature *root_signature;
	ID3D12DescriptorHeap *descriptor_heaps[2];
	ID3D12PipelineState *pipeline;
	ID3D12CommandSignature *command_signature;
	UINT push_constant_slot;
	UINT push_constant_values;
	UINT bindless;
	D3D12_CPU_DESCRIPTOR_HANDLE bind_start;
	UINT bind_increment;
	PyObject *py_cbv_list;
	PyObject *py_srv_list;
	PyObject *py_uav_list;
	PyObject *py_samplers_list;
	ID3D12DescriptorHeap *dsv_descriptor_heap;
	PyObject *py_dsv;
} d3d12_Pipeline;

static PyMemberDef d3d12_Device_members[] = {
	{"name", T_OBJECT_EX, offsetof(d3d12_Device, name), 0, "device name/description"},
	{"dedicated_video_memory", T_ULONGLONG, offsetof(d3d12_Device, dedicated_video_memory), 0, "device dedicated video memory amount"},
	{"dedicated_system_memory", T_ULONGLONG, offsetof(d3d12_Device, dedicated_system_memory), 0, "device dedicated system memory amount"},
	{"shared_system_memory", T_ULONGLONG, offsetof(d3d12_Device, shared_system_memory), 0, "device shared system memory amount"},
	{"vendor_id", T_UINT, offsetof(d3d12_Device, vendor_id), 0, "device VendorId"},
	{"device_id", T_UINT, offsetof(d3d12_Device, vendor_id), 0, "device DeviceId"},
	{"is_hardware", T_BOOL, offsetof(d3d12_Device, is_hardware), 0, "returns True if this is a hardware device and not an emulated/software one"},
	{"is_discrete", T_BOOL, offsetof(d3d12_Device, is_discrete), 0, "returns True if this is a discrete device"},
	{NULL} /* Sentinel */
};

static PyMemberDef d3d12_Resource_members[] = {
	{"size", T_ULONGLONG, offsetof(d3d12_Resource, size), 0, "resource size"},
	{"width", T_UINT, offsetof(d3d12_Resource, footprint) + offsetof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT, Footprint.Width), 0, "resource width"},
	{"height", T_UINT, offsetof(d3d12_Resource, footprint) + offsetof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT, Footprint.Height), 0, "resource height"},
	{"depth", T_UINT, offsetof(d3d12_Resource, footprint) + offsetof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT, Footprint.Depth), 0, "resource depth"},
	{"row_pitch", T_UINT, offsetof(d3d12_Resource, footprint) + offsetof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT, Footprint.RowPitch), 0, "resource row pitch"},
	{"slices", T_UINT, offsetof(d3d12_Resource, slices), 0, "resource number of slices"},
	{"heap_size", T_ULONGLONG, offsetof(d3d12_Resource, heap_size), 0, "resource heap size"},
	{"heap_type", T_INT, offsetof(d3d12_Resource, heap_type), 0, "resource heap type"},
	{"tiles_x", T_UINT, offsetof(d3d12_Resource, tiles_x), 0, "sparsed resource number of tiles on x axis"},
	{"tiles_y", T_UINT, offsetof(d3d12_Resource, tiles_y), 0, "sparsed resource number of tiles on y axis"},
	{"tiles_z", T_UINT, offsetof(d3d12_Resource, tiles_z), 0, "sparsed resource number of tiles on z axis"},
	{"tile_width", T_UINT, offsetof(d3d12_Resource, tile_width), 0, "sparsed resource tile width"},
	{"tile_height", T_UINT, offsetof(d3d12_Resource, tile_height), 0, "sparsed resource tile height"},
	{"tile_depth", T_UINT, offsetof(d3d12_Resource, tile_depth), 0, "sparsed resource tile depth"},
	{NULL} /* Sentinel */
};

static void d3d12_Device_dealloc(d3d12_Device *self)
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

	Py_TYPE(self)->tp_free((PyObject *)self);
}

static d3d12_Device *d3d12_Device_get_device(d3d12_Device *self)
{
	if (self->device)
		return self;

	HRESULT hr = D3D12CreateDevice(self->adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device1), (void **)&self->device);
	if (hr != S_OK)
	{
		d3d_generate_exception(PyExc_Exception, hr, "Unable to create ID3D12Device1");
		return NULL;
	}

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // we need this for swapchain support (TODO: maybe allow the user to configure this or fallback to compute)

	hr = self->device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), (void **)&self->queue);
	if (hr != S_OK)
	{
		self->device->Release();
		self->device = NULL;
		d3d_generate_exception(PyExc_Exception, hr, "Unable to create Command Queue");
		return NULL;
	}

	hr = self->device->CreateFence(self->fence_value, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence1), (void **)&self->fence);
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

	hr = self->device->CreateCommandAllocator(queue_desc.Type, __uuidof(ID3D12CommandAllocator), (void **)&self->command_allocator);
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

	hr = self->device->CreateCommandList(0, queue_desc.Type, self->command_allocator, NULL, __uuidof(ID3D12GraphicsCommandList1), (void **)&self->command_list);
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

COMPUSHADY_TYPE(d3d12, Device);

static void d3d12_Heap_dealloc(d3d12_Heap *self)
{
	if (self->heap)
	{
		self->heap->Release();
	}

	Py_XDECREF(self->py_device);

	Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyMemberDef d3d12_Heap_members[] = {
	{"size", T_ULONGLONG, offsetof(d3d12_Heap, size), 0, "heap size"},
	{"heap_type", T_INT, offsetof(d3d12_Heap, heap_type), 0, "heap type"},
	{NULL} /* Sentinel */
};

COMPUSHADY_TYPE(d3d12, Heap);

static void d3d12_Resource_dealloc(d3d12_Resource *self)
{
	if (self->resource)
	{
		self->resource->Release();
	}

	Py_XDECREF(self->py_heap);

	Py_XDECREF(self->py_device);

	Py_TYPE(self)->tp_free((PyObject *)self);
}

COMPUSHADY_TYPE(d3d12, Resource);

static void d3d12_Swapchain_dealloc(d3d12_Swapchain *self)
{
	if (self->swapchain)
	{
		self->swapchain->Release();
	}

	Py_XDECREF(self->py_device);

	Py_TYPE(self)->tp_free((PyObject *)self);
}

COMPUSHADY_TYPE(d3d12, Swapchain);

static void d3d12_Pipeline_dealloc(d3d12_Pipeline *self)
{
	if (self->command_signature)
		self->command_signature->Release();
	if (self->pipeline)
		self->pipeline->Release();
	if (self->descriptor_heaps[0])
		self->descriptor_heaps[0]->Release();
	if (self->descriptor_heaps[1])
		self->descriptor_heaps[1]->Release();
	if (self->root_signature)
		self->root_signature->Release();

	Py_XDECREF(self->py_device);

	Py_XDECREF(self->py_cbv_list);
	Py_XDECREF(self->py_srv_list);
	Py_XDECREF(self->py_uav_list);
	Py_XDECREF(self->py_samplers_list);

	Py_XDECREF(self->py_dsv);

	Py_TYPE(self)->tp_free((PyObject *)self);
}

COMPUSHADY_TYPE(d3d12, Pipeline);

static void d3d12_Sampler_dealloc(d3d12_Sampler *self)
{
	Py_XDECREF(self->py_device);

	Py_TYPE(self)->tp_free((PyObject *)self);
}

COMPUSHADY_TYPE(d3d12, Sampler);

static PyObject *d3d12_Swapchain_present(d3d12_Swapchain *self, PyObject *args)
{
	PyObject *py_resource;
	uint32_t x;
	uint32_t y;
	if (!PyArg_ParseTuple(args, "OII", &py_resource, &x, &y))
		return NULL;

	int ret = PyObject_IsInstance(py_resource, (PyObject *)&d3d12_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}
	d3d12_Resource *src_resource = (d3d12_Resource *)py_resource;
	if (src_resource->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Texture object");
	}

	UINT index = self->swapchain->GetCurrentBackBufferIndex();

	x = Py_MIN(x, self->desc.Width - 1);
	y = Py_MIN(y, self->desc.Height - 1);

	ID3D12Resource *backbuffer = self->backbuffers[index];

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
	src_location.pResource = ((d3d12_Resource *)py_resource)->resource;
	src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

	D3D12_BOX box = {};
	box.right = Py_MIN(((d3d12_Resource *)py_resource)->footprint.Footprint.Width, self->desc.Width - x);
	box.bottom = Py_MIN(((d3d12_Resource *)py_resource)->footprint.Footprint.Height, self->desc.Height - y);
	box.back = 1;
	self->py_device->command_list->CopyTextureRegion(&dest_location, x, y, 0, &src_location, &box);

	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;

	self->py_device->command_list->ResourceBarrier(2, barriers);

	self->py_device->command_list->Close();

	self->py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList **)&self->py_device->command_list);
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

static PyObject *d3d12_Device_create_swapchain(d3d12_Device *self, PyObject *args)
{
	HWND window_handle;
	DXGI_FORMAT format;
	uint32_t num_buffers;
	uint32_t width = 0;
	uint32_t height = 0;

	if (!PyArg_ParseTuple(args, "KiI|ii", &window_handle, &format, &num_buffers, &width, &height))
		return NULL;

	d3d12_Device *py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	d3d12_Swapchain *py_swapchain = (d3d12_Swapchain *)PyObject_New(d3d12_Swapchain, &d3d12_Swapchain_Type);
	if (!py_swapchain)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Swapchain");
	}
	COMPUSHADY_CLEAR(py_swapchain);
	py_swapchain->py_device = py_device;
	Py_INCREF(py_swapchain->py_device);
	py_swapchain->backbuffers = {};

	IDXGIFactory2 *factory = NULL;
	HRESULT hr = CreateDXGIFactory2(d3d12_debug ? DXGI_CREATE_FACTORY_DEBUG : 0, __uuidof(IDXGIFactory2), (void **)&factory);
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

	hr = factory->CreateSwapChainForHwnd(py_device->queue, window_handle, &swap_chain_desc1, NULL, NULL, (IDXGISwapChain1 **)&py_swapchain->swapchain);
	if (hr != S_OK)
	{
		factory->Release();
		Py_DECREF(py_swapchain);
		return d3d_generate_exception(PyExc_Exception, hr, "unable to create Swapchain");
	}

	if (format == DXGI_FORMAT_R10G10B10A2_UNORM)
	{
		hr = py_swapchain->swapchain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
		if (hr != S_OK)
		{
			factory->Release();
			Py_DECREF(py_swapchain);
			return d3d_generate_exception(PyExc_Exception, hr, "unable to set ColorSpace on Swapchain");
		}
	}

	py_swapchain->swapchain->GetDesc1(&py_swapchain->desc);

	py_swapchain->backbuffers.resize(py_swapchain->desc.BufferCount);
	for (UINT i = 0; i < py_swapchain->desc.BufferCount; i++)
	{
		hr = py_swapchain->swapchain->GetBuffer(i, __uuidof(ID3D12Resource), (void **)&py_swapchain->backbuffers.data()[i]);
		if (hr != S_OK)
		{
			factory->Release();
			Py_DECREF(py_swapchain);
			return d3d_generate_exception(PyExc_Exception, hr, "unable to get Swapchain buffer");
		}
	}

	factory->Release();

	return (PyObject *)py_swapchain;
}

static PyObject *d3d12_Device_create_heap(d3d12_Device *self, PyObject *args)
{
	int heap_type;
	SIZE_T size;

	if (!PyArg_ParseTuple(args, "iK", &heap_type, &size))
		return NULL;

	if (!size)
		return PyErr_Format(Compushady_HeapError, "zero size heap");

	d3d12_Device *py_device = d3d12_Device_get_device(self);
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
		heap_desc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
		;
		break;
	case COMPUSHADY_HEAP_READBACK:
		heap_desc.Properties.Type = D3D12_HEAP_TYPE_READBACK;
		break;
	default:
		return PyErr_Format(Compushady_HeapError, "invalid heap type: %d", heap_type);
	}

	ID3D12Heap *heap = NULL;

	HRESULT hr = py_device->device->CreateHeap(&heap_desc, __uuidof(ID3D12Heap), (void **)&heap);

	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_HeapError, hr, "Unable to create ID3D12Heap");
	}

	d3d12_Heap *py_heap = (d3d12_Heap *)PyObject_New(d3d12_Heap, &d3d12_Heap_Type);
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

	return (PyObject *)py_heap;
}

static PyObject *d3d12_Device_create_buffer(d3d12_Device *self, PyObject *args)
{
	int heap_type;
	SIZE_T size;
	UINT stride;
	DXGI_FORMAT format;
	PyObject *py_heap;
	SIZE_T heap_offset;
	PyObject *py_sparse;
	if (!PyArg_ParseTuple(args, "iKIiOKO", &heap_type, &size, &stride, &format, &py_heap, &heap_offset, &py_sparse))
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

	const bool sparse = py_sparse && PyObject_IsTrue(py_sparse);

	d3d12_Device *py_device = d3d12_Device_get_device(self);
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

	ID3D12Resource1 *resource;
	HRESULT hr;

	D3D12_RESOURCE_ALLOCATION_INFO allocation_info = py_device->device->GetResourceAllocationInfo(0, 1, (D3D12_RESOURCE_DESC *)&resource_desc);

	bool has_heap = false;
	UINT tiles = 0;
	UINT tile_width = 0;
	UINT tile_height = 0;
	UINT tile_depth = 0;

	if (sparse)
	{
		hr = py_device->device->CreateReservedResource((D3D12_RESOURCE_DESC *)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void **)&resource);
	}
	else if (py_heap && py_heap != Py_None)
	{
		int ret = PyObject_IsInstance(py_heap, (PyObject *)&d3d12_Heap_Type);
		if (ret < 0)
		{
			return NULL;
		}
		else if (ret == 0)
		{
			return PyErr_Format(PyExc_ValueError, "Expected a Heap object");
		}

		d3d12_Heap *py_d3d12_heap = (d3d12_Heap *)py_heap;

		if (py_d3d12_heap->py_device != py_device)
		{
			return PyErr_Format(Compushady_BufferError, "Cannot use heap from a different device");
		}

		if (py_d3d12_heap->heap_type != heap_type)
		{
			return PyErr_Format(Compushady_BufferError, "Unsupported heap type");
		}

		if (heap_offset + allocation_info.SizeInBytes > py_d3d12_heap->size)
		{
			return PyErr_Format(Compushady_BufferError,
								"supplied heap is not big enough for the resource size: (offset %llu) %llu "
								"(required %llu)",
								heap_offset, py_d3d12_heap->size, allocation_info.SizeInBytes);
		}
		hr = py_device->device->CreatePlacedResource(py_d3d12_heap->heap, heap_offset, (D3D12_RESOURCE_DESC *)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void **)&resource);
		has_heap = true;
	}
	else
	{
		hr = py_device->device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC *)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void **)&resource);
	}

	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_BufferError, hr, "Unable to create ID3D12Resource1");
	}

	d3d12_Resource *py_resource = (d3d12_Resource *)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		resource->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = resource;
	py_resource->size = size;
	py_resource->dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	py_resource->stride = stride;
	py_resource->format = format;
	py_resource->slices = 1;
	py_resource->heap_size = allocation_info.SizeInBytes;
	py_resource->heap_type = heap_type;

	if (sparse)
	{
		D3D12_TILE_SHAPE shape = {};
		D3D12_SUBRESOURCE_TILING tiling = {};
		UINT num_subresources = 1;
		py_device->device->GetResourceTiling(resource, NULL, NULL, &shape, &num_subresources, 0, &tiling);
		py_resource->tile_width = shape.WidthInTexels;
		py_resource->tile_height = shape.HeightInTexels;
		py_resource->tile_depth = shape.DepthInTexels;
		py_resource->tiles_x = tiling.WidthInTiles;
		py_resource->tiles_y = tiling.HeightInTiles;
		py_resource->tiles_z = tiling.DepthInTiles;
	}

	if (has_heap)
	{
		py_resource->py_heap = (d3d12_Heap *)py_heap;
		Py_INCREF(py_heap);
	}

	return (PyObject *)py_resource;
}

static PyObject *d3d12_Device_create_texture1d(d3d12_Device *self, PyObject *args)
{
	UINT width;
	DXGI_FORMAT format;
	PyObject *py_heap;
	SIZE_T heap_offset;
	UINT slices;
	PyObject *py_sparse;
	if (!PyArg_ParseTuple(args, "IiOKIO", &width, &format, &py_heap, &heap_offset, &slices, &py_sparse))
		return NULL;

	if (width == 0)
	{
		return PyErr_Format(PyExc_ValueError, "invalid width");
	}

	if (slices == 0)
	{
		return PyErr_Format(PyExc_ValueError, "invalid number of slices");
	}

	if (dxgi_pixels_sizes.find(format) == dxgi_pixels_sizes.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	const bool sparse = py_sparse && PyObject_IsTrue(py_sparse);

	d3d12_Device *py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clear_value = {};
	D3D12_CLEAR_VALUE *clear_value_ptr = NULL;

	D3D12_RESOURCE_DESC1 resource_desc = {};
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
	resource_desc.Width = width;
	resource_desc.Height = 1;
	resource_desc.DepthOrArraySize = slices;
	resource_desc.MipLevels = 1;
	resource_desc.Format = format;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resource_desc.SampleDesc.Count = 1;
	resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	if (format == D32_FLOAT || format == D24_UNORM_S8_UINT || format == D16_UNORM)
	{
		if (format == D32_FLOAT)
		{
			resource_desc.Format = DXGI_FORMAT_R32_TYPELESS;
		}
		else if (format == D24_UNORM_S8_UINT)
		{
			resource_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		}
		else if (format == D16_UNORM)
		{
			resource_desc.Format = DXGI_FORMAT_R16_TYPELESS;
		}
		resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		clear_value.Format = format;
		clear_value.DepthStencil.Depth = 1;
		clear_value.DepthStencil.Stencil = 0;
		clear_value_ptr = &clear_value;
	}
	else
	{
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	}

	UINT64 texture_size = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	py_device->device->GetCopyableFootprints((D3D12_RESOURCE_DESC *)&resource_desc, 0, 1, 0, &footprint, NULL, NULL, &texture_size);

	D3D12_RESOURCE_ALLOCATION_INFO allocation_info = py_device->device->GetResourceAllocationInfo(0, 1, (D3D12_RESOURCE_DESC *)&resource_desc);

	ID3D12Resource1 *resource;
	HRESULT hr;
	bool has_heap = false;

	if (sparse)
	{
		hr = py_device->device->CreateReservedResource((D3D12_RESOURCE_DESC *)&resource_desc, state, clear_value_ptr, __uuidof(ID3D12Resource1), (void **)&resource);
	}
	else if (py_heap && py_heap != Py_None)
	{
		int ret = PyObject_IsInstance(py_heap, (PyObject *)&d3d12_Heap_Type);
		if (ret < 0)
		{
			return NULL;
		}
		else if (ret == 0)
		{
			return PyErr_Format(PyExc_ValueError, "Expected a Heap object");
		}

		d3d12_Heap *py_d3d12_heap = (d3d12_Heap *)py_heap;

		if (py_d3d12_heap->py_device != py_device)
		{
			return PyErr_Format(Compushady_Texture1DError, "Cannot use heap from a different device");
		}

		if (py_d3d12_heap->heap_type != COMPUSHADY_HEAP_DEFAULT)
		{
			return PyErr_Format(Compushady_Texture1DError, "Unsupported heap type %d != %d", py_d3d12_heap->heap_type, heap_properties.Type);
		}

		if (heap_offset + allocation_info.SizeInBytes > py_d3d12_heap->size)
		{
			return PyErr_Format(Compushady_Texture1DError,
								"supplied heap is not big enough for the resource size: (offset %llu) %llu "
								"(required %llu)",
								heap_offset, py_d3d12_heap->size, allocation_info.SizeInBytes);
		}
		hr = py_device->device->CreatePlacedResource(py_d3d12_heap->heap, heap_offset, (D3D12_RESOURCE_DESC *)&resource_desc, state, clear_value_ptr, __uuidof(ID3D12Resource1), (void **)&resource);
		has_heap = true;
	}
	else
	{
		hr = py_device->device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC *)&resource_desc, state, clear_value_ptr, __uuidof(ID3D12Resource1), (void **)&resource);
	}

	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_Texture1DError, hr, "Unable to create ID3D12Resource1");
	}

	d3d12_Resource *py_resource = (d3d12_Resource *)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		resource->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = self;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = resource;
	py_resource->footprint = footprint;
	py_resource->size = texture_size;
	py_resource->dimension = resource_desc.Dimension;
	py_resource->stride = 0;
	py_resource->format = format;
	py_resource->slices = slices;
	py_resource->heap_size = allocation_info.SizeInBytes;
	py_resource->heap_type = COMPUSHADY_HEAP_DEFAULT;

	if (sparse)
	{
		D3D12_TILE_SHAPE shape = {};
		D3D12_SUBRESOURCE_TILING tiling = {};
		UINT num_subresources = 1;
		py_device->device->GetResourceTiling(resource, NULL, NULL, &shape, &num_subresources, 0, &tiling);
		py_resource->tile_width = shape.WidthInTexels;
		py_resource->tile_height = shape.HeightInTexels;
		py_resource->tile_depth = shape.DepthInTexels;
		py_resource->tiles_x = tiling.WidthInTiles;
		py_resource->tiles_y = tiling.HeightInTiles;
		py_resource->tiles_z = tiling.DepthInTiles;
	}

	if (has_heap)
	{
		py_resource->py_heap = (d3d12_Heap *)py_heap;
		Py_INCREF(py_heap);
	}

	return (PyObject *)py_resource;
}

#define COMPUSHADY_D3D12_SAMPLER_ADDRESS_MODE(var, field)                                    \
	if (var == COMPUSHADY_SAMPLER_ADDRESS_MODE_WRAP)                                         \
	{                                                                                        \
		var = D3D12_TEXTURE_ADDRESS_MODE_WRAP;                                               \
	}                                                                                        \
	else if (var == COMPUSHADY_SAMPLER_ADDRESS_MODE_MIRROR)                                  \
	{                                                                                        \
		var = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;                                             \
	}                                                                                        \
	else if (var == COMPUSHADY_SAMPLER_ADDRESS_MODE_CLAMP)                                   \
	{                                                                                        \
		var = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;                                              \
	}                                                                                        \
	else                                                                                     \
	{                                                                                        \
		return PyErr_Format(Compushady_SamplerError, "unsupported address mode for " field); \
	}

static PyObject *d3d12_Device_create_sampler(d3d12_Device *self, PyObject *args)
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

	d3d12_Device *py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	d3d12_Sampler *py_sampler = (d3d12_Sampler *)PyObject_New(d3d12_Sampler, &d3d12_Sampler_Type);
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

	return (PyObject *)py_sampler;
}

static PyObject *d3d12_Device_create_texture2d(d3d12_Device *self, PyObject *args)
{
	UINT width;
	UINT height;
	DXGI_FORMAT format;
	PyObject *py_heap;
	SIZE_T heap_offset;
	UINT slices;
	PyObject *py_sparse;
	if (!PyArg_ParseTuple(args, "IIiOKIO", &width, &height, &format, &py_heap, &heap_offset, &slices, &py_sparse))
		return NULL;

	if (width == 0)
	{
		return PyErr_Format(PyExc_ValueError, "invalid width");
	}

	if (height == 0)
	{
		return PyErr_Format(PyExc_ValueError, "invalid height");
	}

	if (slices == 0)
	{
		return PyErr_Format(PyExc_ValueError, "invalid number of slices");
	}

	if (dxgi_pixels_sizes.find(format) == dxgi_pixels_sizes.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	const bool sparse = py_sparse && PyObject_IsTrue(py_sparse);

	d3d12_Device *py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clear_value = {};
	D3D12_CLEAR_VALUE *clear_value_ptr = NULL;

	D3D12_RESOURCE_DESC1 resource_desc = {};
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resource_desc.Width = width;
	resource_desc.Height = height;
	resource_desc.DepthOrArraySize = slices;
	resource_desc.MipLevels = 1;
	resource_desc.Format = format;
	resource_desc.Layout = sparse ? D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE : D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resource_desc.SampleDesc.Count = 1;
	resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	if (format == D32_FLOAT || format == D24_UNORM_S8_UINT || format == D16_UNORM)
	{
		if (format == D32_FLOAT)
		{
			resource_desc.Format = DXGI_FORMAT_R32_TYPELESS;
		}
		else if (format == D24_UNORM_S8_UINT)
		{
			resource_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		}
		else if (format == D16_UNORM)
		{
			resource_desc.Format = DXGI_FORMAT_R16_TYPELESS;
		}
		resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		clear_value.Format = format;
		clear_value.DepthStencil.Depth = 1;
		clear_value.DepthStencil.Stencil = 0;
		clear_value_ptr = &clear_value;
	}
	else
	{
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	}

	UINT64 texture_size = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	py_device->device->GetCopyableFootprints((D3D12_RESOURCE_DESC *)&resource_desc, 0, 1, 0, &footprint, NULL, NULL, &texture_size);

	D3D12_RESOURCE_ALLOCATION_INFO allocation_info = py_device->device->GetResourceAllocationInfo(0, 1, (D3D12_RESOURCE_DESC *)&resource_desc);

	ID3D12Resource1 *resource;
	HRESULT hr;
	bool has_heap = false;

	if (sparse)
	{
		hr = py_device->device->CreateReservedResource((D3D12_RESOURCE_DESC *)&resource_desc, state, clear_value_ptr, __uuidof(ID3D12Resource1), (void **)&resource);
	}
	else if (py_heap && py_heap != Py_None)
	{
		int ret = PyObject_IsInstance(py_heap, (PyObject *)&d3d12_Heap_Type);
		if (ret < 0)
		{
			return NULL;
		}
		else if (ret == 0)
		{
			return PyErr_Format(PyExc_ValueError, "Expected a Heap object");
		}

		d3d12_Heap *py_d3d12_heap = (d3d12_Heap *)py_heap;

		if (py_d3d12_heap->py_device != py_device)
		{
			return PyErr_Format(Compushady_Texture2DError, "Cannot use heap from a different device");
		}

		if (py_d3d12_heap->heap_type != COMPUSHADY_HEAP_DEFAULT)
		{
			return PyErr_Format(Compushady_Texture2DError, "Unsupported heap type");
		}

		if (heap_offset + allocation_info.SizeInBytes > py_d3d12_heap->size)
		{
			return PyErr_Format(Compushady_Texture2DError,
								"supplied heap is not big enough for the resource size: (offset %llu) %llu "
								"(required %llu)",
								heap_offset, py_d3d12_heap->size, allocation_info.SizeInBytes);
		}
		hr = py_device->device->CreatePlacedResource(py_d3d12_heap->heap, heap_offset, (D3D12_RESOURCE_DESC *)&resource_desc, state, clear_value_ptr, __uuidof(ID3D12Resource1), (void **)&resource);
		has_heap = true;
	}
	else
	{
		hr = py_device->device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC *)&resource_desc, state, clear_value_ptr, __uuidof(ID3D12Resource1), (void **)&resource);
	}

	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_Texture2DError, hr, "Unable to create ID3D12Resource1");
	}

	d3d12_Resource *py_resource = (d3d12_Resource *)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		resource->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = self;
	Py_INCREF(py_resource->py_device);

	py_resource->resource = resource;
	py_resource->footprint = footprint;
	py_resource->size = texture_size;
	py_resource->dimension = resource_desc.Dimension;
	py_resource->stride = 0;
	py_resource->format = format;
	py_resource->slices = slices;
	py_resource->heap_size = allocation_info.SizeInBytes;
	py_resource->heap_type = COMPUSHADY_HEAP_DEFAULT;

	if (sparse)
	{
		D3D12_TILE_SHAPE shape = {};
		D3D12_SUBRESOURCE_TILING tiling = {};
		UINT num_subresources = 1;
		py_device->device->GetResourceTiling(resource, NULL, NULL, &shape, &num_subresources, 0, &tiling);
		py_resource->tile_width = shape.WidthInTexels;
		py_resource->tile_height = shape.HeightInTexels;
		py_resource->tile_depth = shape.DepthInTexels;
		py_resource->tiles_x = tiling.WidthInTiles;
		py_resource->tiles_y = tiling.HeightInTiles;
		py_resource->tiles_z = tiling.DepthInTiles;
	}

	if (has_heap)
	{
		py_resource->py_heap = (d3d12_Heap *)py_heap;
		Py_INCREF(py_heap);
	}

	return (PyObject *)py_resource;
}

static PyObject *d3d12_Device_create_texture3d(d3d12_Device *self, PyObject *args)
{
	UINT width;
	UINT height;
	UINT depth;
	DXGI_FORMAT format;
	PyObject *py_heap;
	SIZE_T heap_offset;
	PyObject *py_sparse;
	if (!PyArg_ParseTuple(args, "IIIiOKO", &width, &height, &depth, &format, &py_heap, &heap_offset, &py_sparse))
		return NULL;

	if (width == 0)
	{
		return PyErr_Format(PyExc_ValueError, "invalid width");
	}

	if (height == 0)
	{
		return PyErr_Format(PyExc_ValueError, "invalid height");
	}

	if (depth == 0)
	{
		return PyErr_Format(PyExc_ValueError, "invalid depth");
	}

	if (dxgi_pixels_sizes.find(format) == dxgi_pixels_sizes.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	const bool sparse = py_sparse && PyObject_IsTrue(py_sparse);

	d3d12_Device *py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

	D3D12_HEAP_PROPERTIES heap_properties = {};
	heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clear_value = {};
	D3D12_CLEAR_VALUE *clear_value_ptr = NULL;

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

	if (format == D32_FLOAT || format == D24_UNORM_S8_UINT || format == D16_UNORM)
	{
		if (format == D32_FLOAT)
		{
			resource_desc.Format = DXGI_FORMAT_R32_TYPELESS;
		}
		else if (format == D24_UNORM_S8_UINT)
		{
			resource_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		}
		else if (format == D16_UNORM)
		{
			resource_desc.Format = DXGI_FORMAT_R16_TYPELESS;
		}
		resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		clear_value.Format = format;
		clear_value.DepthStencil.Depth = 1;
		clear_value.DepthStencil.Stencil = 0;
		clear_value_ptr = &clear_value;
	}
	else
	{
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	}

	UINT64 texture_size = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
	py_device->device->GetCopyableFootprints((D3D12_RESOURCE_DESC *)&resource_desc, 0, 1, 0, &footprint, NULL, NULL, &texture_size);

	D3D12_RESOURCE_ALLOCATION_INFO allocation_info = py_device->device->GetResourceAllocationInfo(0, 1, (D3D12_RESOURCE_DESC *)&resource_desc);

	ID3D12Resource1 *resource;
	HRESULT hr;
	bool has_heap = false;

	if (sparse)
	{
		hr = py_device->device->CreateReservedResource((D3D12_RESOURCE_DESC *)&resource_desc, state, clear_value_ptr, __uuidof(ID3D12Resource1), (void **)&resource);
	}
	else if (py_heap && py_heap != Py_None)
	{
		int ret = PyObject_IsInstance(py_heap, (PyObject *)&d3d12_Heap_Type);
		if (ret < 0)
		{
			return NULL;
		}
		else if (ret == 0)
		{
			return PyErr_Format(PyExc_ValueError, "Expected a Heap object");
		}

		d3d12_Heap *py_d3d12_heap = (d3d12_Heap *)py_heap;

		if (py_d3d12_heap->py_device != py_device)
		{
			return PyErr_Format(Compushady_Texture3DError, "Cannot use heap from a different device");
		}

		if (py_d3d12_heap->heap_type != COMPUSHADY_HEAP_DEFAULT)
		{
			return PyErr_Format(Compushady_Texture3DError, "Unsupported heap type");
		}

		if (heap_offset + allocation_info.SizeInBytes > py_d3d12_heap->size)
		{
			return PyErr_Format(Compushady_Texture3DError,
								"supplied heap is not big enough for the resource size: (offset %llu) %llu "
								"(required %llu)",
								heap_offset, py_d3d12_heap->size, allocation_info.SizeInBytes);
		}
		hr = py_device->device->CreatePlacedResource(py_d3d12_heap->heap, heap_offset, (D3D12_RESOURCE_DESC *)&resource_desc, state, clear_value_ptr, __uuidof(ID3D12Resource1), (void **)&resource);
		has_heap = true;
	}
	else
	{
		hr = py_device->device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC *)&resource_desc, state, clear_value_ptr, __uuidof(ID3D12Resource1), (void **)&resource);
	}

	if (hr != S_OK)
	{
		return d3d_generate_exception(Compushady_Texture3DError, hr, "Unable to create ID3D12Resource1");
	}

	d3d12_Resource *py_resource = (d3d12_Resource *)PyObject_New(d3d12_Resource, &d3d12_Resource_Type);
	if (!py_resource)
	{
		resource->Release();
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Resource");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = self;
	Py_INCREF(py_resource->py_device);
	py_resource->resource = resource;

	py_resource->footprint = footprint;
	py_resource->size = texture_size;
	py_resource->dimension = resource_desc.Dimension;
	py_resource->stride = 0;
	py_resource->format = format;
	py_resource->slices = 1;
	py_resource->heap_size = allocation_info.SizeInBytes;
	py_resource->heap_type = COMPUSHADY_HEAP_DEFAULT;

	if (sparse)
	{
		D3D12_TILE_SHAPE shape = {};
		D3D12_SUBRESOURCE_TILING tiling = {};
		UINT num_subresources = 1;
		py_device->device->GetResourceTiling(resource, NULL, NULL, &shape, &num_subresources, 0, &tiling);
		py_resource->tile_width = shape.WidthInTexels;
		py_resource->tile_height = shape.HeightInTexels;
		py_resource->tile_depth = shape.DepthInTexels;
		py_resource->tiles_x = tiling.WidthInTiles;
		py_resource->tiles_y = tiling.HeightInTiles;
		py_resource->tiles_z = tiling.DepthInTiles;
	}

	if (has_heap)
	{
		py_resource->py_heap = (d3d12_Heap *)py_heap;
		Py_INCREF(py_heap);
	}

	return (PyObject *)py_resource;
}

static PyObject *d3d12_Device_get_debug_messages(d3d12_Device *self, PyObject *args)
{
	PyObject *py_list = PyList_New(0);

	// no device -> no debug messages
	if (!self->device)
	{
		return py_list;
	}

	ID3D12InfoQueue *queue;
	HRESULT hr = self->device->QueryInterface<ID3D12InfoQueue>(&queue);
	if (hr == S_OK)
	{
		UINT64 num_of_messages = queue->GetNumStoredMessages();
		for (UINT64 i = 0; i < num_of_messages; i++)
		{
			SIZE_T message_size = 0;
			queue->GetMessage(i, NULL, &message_size);
			D3D12_MESSAGE *message = (D3D12_MESSAGE *)PyMem_Malloc(message_size);
			queue->GetMessage(i, message, &message_size);
			PyObject *py_debug_message = PyUnicode_FromStringAndSize(message->pDescription, message->DescriptionByteLength - 1);
			PyMem_Free(message);
			PyList_Append(py_list, py_debug_message);
			Py_DECREF(py_debug_message);
		}
		queue->ClearStoredMessages();
	}

	return py_list;
}

static d3d12_Pipeline *compushady_d3d12_setup_pipeline(d3d12_Device *self, PyObject *py_cbv, PyObject *py_srv, PyObject *py_uav, PyObject *py_samplers, const UINT push_size, const UINT bindless)
{
	d3d12_Device *py_device = d3d12_Device_get_device(self);
	if (!py_device)
		return NULL;

	std::vector<d3d12_Resource *> cbv;
	std::vector<d3d12_Resource *> srv;
	std::vector<d3d12_Resource *> uav;
	std::vector<d3d12_Sampler *> samplers;

	if (!compushady_check_descriptors(&d3d12_Resource_Type, py_cbv, cbv, py_srv, srv, py_uav, uav, &d3d12_Sampler_Type, py_samplers, samplers))
	{
		return NULL;
	}

	std::vector<D3D12_DESCRIPTOR_RANGE1> ranges;
	std::vector<D3D12_DESCRIPTOR_RANGE1> samplers_ranges;

	if (bindless == 0)
	{
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
	}
	else
	{
		if (cbv.size() > bindless)
		{
			return (d3d12_Pipeline *)PyErr_Format(PyExc_ValueError, "Invalid number of initial CBVs (%u) for bindless Pipeline (max: %u)", (UINT)cbv.size(), bindless);
		}

		if (srv.size() > bindless)
		{
			return (d3d12_Pipeline *)PyErr_Format(PyExc_ValueError, "Invalid number of initial SRVs (%u) for bindless Pipeline (max: %u)", (UINT)srv.size(), bindless);
		}

		if (uav.size() > bindless)
		{
			return (d3d12_Pipeline *)PyErr_Format(PyExc_ValueError, "Invalid number of initial UAVs (%u) for bindless Pipeline (max: %u)", (UINT)uav.size(), bindless);
		}

		D3D12_DESCRIPTOR_RANGE1 range = {};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
		range.NumDescriptors = bindless;
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		ranges.push_back(range);
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges.push_back(range);
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
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

	std::vector<D3D12_ROOT_PARAMETER1> root_parameters;

	if (ranges.size() > 0)
	{
		D3D12_ROOT_PARAMETER1 root_parameter = {};
		root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		root_parameter.DescriptorTable.NumDescriptorRanges = (UINT)ranges.size();
		root_parameter.DescriptorTable.pDescriptorRanges = ranges.data();
		root_parameters.push_back(root_parameter);
	}

	if (samplers_ranges.size() > 0)
	{
		D3D12_ROOT_PARAMETER1 root_parameter = {};
		root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		root_parameter.DescriptorTable.NumDescriptorRanges = (UINT)samplers_ranges.size();
		root_parameter.DescriptorTable.pDescriptorRanges = samplers_ranges.data();
		root_parameters.push_back(root_parameter);
	}

	UINT push_constant_slot = 0;
	UINT push_constant_values = 0;

	if (push_size > 0)
	{
		D3D12_ROOT_PARAMETER1 root_parameter = {};
		root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		root_parameter.Constants.Num32BitValues = (UINT)ceil(push_size / 4.0);
		root_parameter.Constants.ShaderRegister = (UINT)cbv.size();
		push_constant_slot = (UINT)root_parameters.size();
		push_constant_values = root_parameter.Constants.Num32BitValues;
		root_parameters.push_back(root_parameter);
	}

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_root_signature = {};
	versioned_root_signature.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	versioned_root_signature.Desc_1_1.NumParameters = (UINT)root_parameters.size();
	versioned_root_signature.Desc_1_1.pParameters = root_parameters.data();
	versioned_root_signature.Desc_1_1.Flags = bindless > 0 ? D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED : D3D12_ROOT_SIGNATURE_FLAG_NONE;

	ID3DBlob *serialized_root_signature;
	HRESULT hr = D3D12SerializeVersionedRootSignature(&versioned_root_signature, &serialized_root_signature, NULL);
	if (hr != S_OK)
	{
		return (d3d12_Pipeline *)d3d_generate_exception(PyExc_Exception, hr, "Unable to serialize Versioned Root Signature");
	}

	d3d12_Pipeline *py_pipeline = (d3d12_Pipeline *)PyObject_New(d3d12_Pipeline, &d3d12_Pipeline_Type);
	if (!py_pipeline)
	{
		return (d3d12_Pipeline *)PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Compute Pipeline");
	}
	COMPUSHADY_CLEAR(py_pipeline);
	py_pipeline->py_device = py_device;
	Py_INCREF(py_pipeline->py_device);

	py_pipeline->push_constant_slot = push_constant_slot;
	py_pipeline->push_constant_values = push_constant_values;

	hr = py_device->device->CreateRootSignature(0, serialized_root_signature->GetBufferPointer(), serialized_root_signature->GetBufferSize(), __uuidof(ID3D12RootSignature), (void **)&py_pipeline->root_signature);
	serialized_root_signature->Release();
	if (hr != S_OK)
	{
		Py_DECREF(py_pipeline);
		return (d3d12_Pipeline *)d3d_generate_exception(PyExc_Exception, hr, "Unable to create Root Signature");
	}

	const UINT num_descriptors = (bindless > 0) ? (bindless * 3) : ((UINT)(cbv.size() + srv.size() + uav.size()));
	if (num_descriptors > 0)
	{
		D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
		descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		descriptor_heap_desc.NumDescriptors = num_descriptors;

		hr = py_device->device->CreateDescriptorHeap(&descriptor_heap_desc, __uuidof(ID3D12DescriptorHeap), (void **)&py_pipeline->descriptor_heaps[0]);
		if (hr != S_OK)
		{
			Py_DECREF(py_pipeline);
			return (d3d12_Pipeline *)d3d_generate_exception(PyExc_Exception, hr, "Unable to create Descriptor Heap");
		}

		py_pipeline->bind_increment = py_device->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		py_pipeline->bind_start = py_pipeline->descriptor_heaps[0]->GetCPUDescriptorHandleForHeapStart();

		D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = py_pipeline->bind_start;

		for (d3d12_Resource *resource : cbv)
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
			cbv_desc.BufferLocation = resource->resource->GetGPUVirtualAddress();
			cbv_desc.SizeInBytes = (UINT)COMPUSHADY_ALIGN(resource->size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
			py_device->device->CreateConstantBufferView(&cbv_desc, cpu_handle);
			cpu_handle.ptr += py_pipeline->bind_increment;
		}

		if (bindless > 0)
		{
			cpu_handle.ptr = py_pipeline->bind_start.ptr + (bindless * py_pipeline->bind_increment);
		}

		for (d3d12_Resource *resource : srv)
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
			else if ((resource->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D || resource->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) && (resource->format == D32_FLOAT || resource->format == D24_UNORM_S8_UINT || resource->format == D16_UNORM))
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
				if (resource->slices > 1)
				{
					if (resource->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
					{
						srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
						srv_desc.Texture1DArray.ArraySize = resource->slices;
						srv_desc.Texture1DArray.MipLevels = 1;
					}
					else if (resource->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
					{
						srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
						srv_desc.Texture2DArray.ArraySize = resource->slices;
						srv_desc.Texture2DArray.MipLevels = 1;
					}
				}
				else
				{
					if (resource->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
					{
						srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
						srv_desc.Texture1D.MipLevels = 1;
					}
					else if (resource->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
					{
						srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
						srv_desc.Texture2D.MipLevels = 1;
					}
				}
				srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				if (resource->format == D32_FLOAT)
				{
					srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
				}
				else if (resource->format == D24_UNORM_S8_UINT)
				{
					srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
				}
				else if (resource->format == D16_UNORM)
				{
					srv_desc.Format = DXGI_FORMAT_R16_UNORM;
				}
				py_device->device->CreateShaderResourceView(resource->resource, &srv_desc, cpu_handle);
			}
			else
			{
				py_device->device->CreateShaderResourceView(resource->resource, NULL, cpu_handle);
			}
			cpu_handle.ptr += py_pipeline->bind_increment;
		}

		if (bindless > 0)
		{
			cpu_handle.ptr = py_pipeline->bind_start.ptr + (bindless * py_pipeline->bind_increment * 2);
		}

		for (d3d12_Resource *resource : uav)
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
			cpu_handle.ptr += py_pipeline->bind_increment;
		}
	}

	if (samplers.size() > 0)
	{
		D3D12_DESCRIPTOR_HEAP_DESC sampler_descriptor_heap_desc = {};
		sampler_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		sampler_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		sampler_descriptor_heap_desc.NumDescriptors = (UINT)samplers.size();

		const UINT sampler_descriptor_index = py_pipeline->descriptor_heaps[0] ? 1 : 0;

		hr = py_device->device->CreateDescriptorHeap(&sampler_descriptor_heap_desc, __uuidof(ID3D12DescriptorHeap), (void **)&py_pipeline->descriptor_heaps[sampler_descriptor_index]);
		if (hr != S_OK)
		{
			Py_DECREF(py_pipeline);
			return (d3d12_Pipeline *)d3d_generate_exception(PyExc_Exception, hr, "Unable to create Sampler Descriptor Heap");
		}

		UINT sampler_increment = py_device->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_handle = py_pipeline->descriptor_heaps[sampler_descriptor_index]->GetCPUDescriptorHandleForHeapStart();

		for (d3d12_Sampler *sampler : samplers)
		{
			py_device->device->CreateSampler(&sampler->sampler_desc, sampler_cpu_handle);
			sampler_cpu_handle.ptr += sampler_increment;
		}
	}

	py_pipeline->bindless = bindless;

	const size_t num_cbv = (bindless > 0) ? bindless : cbv.size();
	const size_t num_srv = (bindless > 0) ? bindless : srv.size();
	const size_t num_uav = (bindless > 0) ? bindless : uav.size();

	py_pipeline->py_cbv_list = PyList_New(num_cbv);
	py_pipeline->py_srv_list = PyList_New(num_srv);
	py_pipeline->py_uav_list = PyList_New(num_uav);
	py_pipeline->py_samplers_list = PyList_New(0);

	for (size_t i = 0; i < num_cbv; i++)
	{
		if (i < cbv.size())
		{
			Py_INCREF((PyObject *)cbv[i]);
			PyList_SetItem(py_pipeline->py_cbv_list, i, (PyObject *)cbv[i]);
		}
		else
		{
			Py_INCREF(Py_None);
			PyList_SetItem(py_pipeline->py_cbv_list, i, Py_None);
		}
	}

	for (size_t i = 0; i < num_srv; i++)
	{
		if (i < srv.size())
		{
			Py_INCREF((PyObject *)srv[i]);
			PyList_SetItem(py_pipeline->py_srv_list, i, (PyObject *)srv[i]);
		}
		else
		{
			Py_INCREF(Py_None);
			PyList_SetItem(py_pipeline->py_srv_list, i, Py_None);
		}
	}

	for (size_t i = 0; i < num_uav; i++)
	{
		if (i < uav.size())
		{
			Py_INCREF((PyObject *)uav[i]);
			PyList_SetItem(py_pipeline->py_uav_list, i, (PyObject *)uav[i]);
		}
		else
		{
			Py_INCREF(Py_None);
			PyList_SetItem(py_pipeline->py_uav_list, i, Py_None);
		}
	}

	for (d3d12_Sampler *py_sampler : samplers)
	{
		PyList_Append(py_pipeline->py_samplers_list, (PyObject *)py_sampler);
	}

	return py_pipeline;
}

static PyObject *d3d12_Device_create_compute(d3d12_Device *self, PyObject *args, PyObject *kwds)
{
	const char *kwlist[] = {"shader", "cbv", "srv", "uav", "samplers", "push_size", "bindless", NULL};
	Py_buffer view;
	PyObject *py_cbv = NULL;
	PyObject *py_srv = NULL;
	PyObject *py_uav = NULL;
	PyObject *py_samplers = NULL;
	UINT push_size = 0;
	UINT bindless = 0;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*OOOOII:create_compute", (char **)kwlist,
									 &view, &py_cbv, &py_srv, &py_uav, &py_samplers, &push_size, &bindless))
		return NULL;

	d3d12_Pipeline *py_compute = compushady_d3d12_setup_pipeline(self, py_cbv, py_srv, py_uav, py_samplers, push_size, bindless);
	if (!py_compute)
	{
		PyBuffer_Release(&view);
		return NULL;
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC compute_pipeline_desc = {};
	compute_pipeline_desc.pRootSignature = py_compute->root_signature;
	compute_pipeline_desc.CS.pShaderBytecode = view.buf;
	compute_pipeline_desc.CS.BytecodeLength = view.len;

	HRESULT hr = py_compute->py_device->device->CreateComputePipelineState(&compute_pipeline_desc, __uuidof(ID3D12PipelineState), (void **)&py_compute->pipeline);
	PyBuffer_Release(&view);
	if (hr != S_OK)
	{
		Py_DECREF(py_compute);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Compute Pipeline State");
	}

	D3D12_INDIRECT_ARGUMENT_DESC indirect_argument_desc = {};
	indirect_argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
	D3D12_COMMAND_SIGNATURE_DESC command_signature_desc = {};
	command_signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
	command_signature_desc.NumArgumentDescs = 1;
	command_signature_desc.pArgumentDescs = &indirect_argument_desc;
	hr = py_compute->py_device->device->CreateCommandSignature(&command_signature_desc, NULL, __uuidof(ID3D12CommandSignature), (void **)&py_compute->command_signature);
	if (hr != S_OK)
	{
		Py_DECREF(py_compute);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Compute Command Signature");
	}

	return (PyObject *)py_compute;
}

template <typename T>
void compushady_d3d12_append_subobject_to_stream(std::vector<char> &stream, const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE subobject_type, const T &subobject)
{
	const size_t ptr_size = sizeof(void *);

	if (stream.size() % ptr_size != 0)
	{
		stream.resize(stream.size() + (ptr_size - (stream.size() % ptr_size)));
	}

	std::vector<char> subobject_type_data(reinterpret_cast<const char *>(&subobject_type), reinterpret_cast<const char *>(&subobject_type) + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE));
	stream.insert(stream.end(), subobject_type_data.begin(), subobject_type_data.end());

	if (stream.size() % alignof(T) != 0)
	{
		stream.resize(stream.size() + (alignof(T) - (stream.size() % alignof(T))));
	}

	std::vector<char> subobject_data(reinterpret_cast<const char *>(&subobject), reinterpret_cast<const char *>(&subobject) + sizeof(T));
	stream.insert(stream.end(), subobject_data.begin(), subobject_data.end());
}

static PyObject *d3d12_Device_create_rasterizer(d3d12_Device *self, PyObject *args, PyObject *kwds)
{
	const char *kwlist[] = {"vs", "ps", "ms", "rtv", "dsv", "cbv", "srv", "uav", "samplers", "push_size", "bindless", NULL};
	Py_buffer vs_view;
	Py_buffer ps_view;
	Py_buffer ms_view;
	PyObject *py_cbv = NULL;
	PyObject *py_srv = NULL;
	PyObject *py_uav = NULL;
	PyObject *py_rtv = NULL;
	PyObject *py_dsv = NULL;
	PyObject *py_samplers = NULL;
	UINT push_size = 0;
	UINT bindless = 0;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*y*y*OOOOOOII:create_rasterizer", (char **)kwlist,
									 &vs_view, &ps_view, &ms_view, &py_rtv, &py_dsv, &py_cbv, &py_srv, &py_uav, &py_samplers, &push_size, &bindless))
		return NULL;

	d3d12_Pipeline *py_rasterizer = compushady_d3d12_setup_pipeline(self, py_cbv, py_srv, py_uav, py_samplers, push_size, bindless);
	if (!py_rasterizer)
	{
		if (vs_view.buf)
			PyBuffer_Release(&vs_view);
		if (ps_view.buf)
			PyBuffer_Release(&ps_view);
		if (ms_view.buf)
			PyBuffer_Release(&ms_view);
		return NULL;
	}

	if (py_dsv)
	{
		const int ret = PyObject_IsInstance(py_dsv, (PyObject *)&d3d12_Resource_Type);
		if (ret < 0)
		{
			if (vs_view.buf)
				PyBuffer_Release(&vs_view);
			if (ps_view.buf)
				PyBuffer_Release(&ps_view);
			if (ms_view.buf)
				PyBuffer_Release(&ms_view);
			Py_DECREF(py_rasterizer);
			return NULL;
		}
		else if (ret == 0)
		{
			if (vs_view.buf)
				PyBuffer_Release(&vs_view);
			if (ps_view.buf)
				PyBuffer_Release(&ps_view);
			if (ms_view.buf)
				PyBuffer_Release(&ms_view);
			Py_DECREF(py_rasterizer);
			return PyErr_Format(PyExc_ValueError, "Expected a Resource object as DSV");
		}

		py_rasterizer->py_dsv = py_dsv;

		D3D12_DESCRIPTOR_HEAP_DESC dsv_descriptor_heap_desc = {};
		dsv_descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsv_descriptor_heap_desc.NumDescriptors = 1;
		dsv_descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

		HRESULT hr = py_rasterizer->py_device->device->CreateDescriptorHeap(&dsv_descriptor_heap_desc, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void **>(&py_rasterizer->dsv_descriptor_heap));
		if (hr != S_OK)
		{
			if (vs_view.buf)
				PyBuffer_Release(&vs_view);
			if (ps_view.buf)
				PyBuffer_Release(&ps_view);
			if (ms_view.buf)
				PyBuffer_Release(&ms_view);
			Py_DECREF(py_rasterizer);
			return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Heap Descriptor for DSV");
		}

		d3d12_Resource *dsv = (d3d12_Resource *)py_rasterizer->py_dsv;
		D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
		if (dsv->slices > 1)
		{
			if (dsv->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
			{
				dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
				dsv_desc.Texture1DArray.ArraySize = dsv->slices;
			}
			else if (dsv->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			{
				dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
				dsv_desc.Texture2DArray.ArraySize = dsv->slices;
			}
		}
		else
		{
			if (dsv->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
			{
				dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
			}
			else if (dsv->dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
			{
				dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			}
		}
		dsv_desc.Format = dsv->format;
		py_rasterizer->py_device->device->CreateDepthStencilView(dsv->resource, &dsv_desc, py_rasterizer->dsv_descriptor_heap->GetCPUDescriptorHandleForHeapStart());
	}

	ID3D12Device2 *device2;
	HRESULT hr = py_rasterizer->py_device->device->QueryInterface<ID3D12Device2>(&device2);
	if (hr != S_OK)
	{
		if (vs_view.buf)
			PyBuffer_Release(&vs_view);
		if (ps_view.buf)
			PyBuffer_Release(&ps_view);
		if (ms_view.buf)
			PyBuffer_Release(&ms_view);
		Py_DECREF(py_rasterizer);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Rasterizer Pipeline State");
	}

	std::vector<char> pipeline_stream;
	compushady_d3d12_append_subobject_to_stream(pipeline_stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, py_rasterizer->root_signature);

	if (vs_view.buf && vs_view.len > 0)
	{
		D3D12_SHADER_BYTECODE vs;
		vs.pShaderBytecode = vs_view.buf;
		vs.BytecodeLength = vs_view.len;
		compushady_d3d12_append_subobject_to_stream(pipeline_stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, vs);
	}

	if (ps_view.buf && ps_view.len > 0)
	{
		D3D12_SHADER_BYTECODE ps;
		ps.pShaderBytecode = ps_view.buf;
		ps.BytecodeLength = ps_view.len;
		compushady_d3d12_append_subobject_to_stream(pipeline_stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, ps);
	}

	if (ms_view.buf && ms_view.len > 0)
	{
		D3D12_SHADER_BYTECODE ms;
		ms.pShaderBytecode = ms_view.buf;
		ms.BytecodeLength = ms_view.len;
		compushady_d3d12_append_subobject_to_stream(pipeline_stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, ms);
	}

	// topology
	compushady_d3d12_append_subobject_to_stream(pipeline_stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

#if 0
	// render target formats
	D3D12_RT_FORMAT_ARRAY rts = {};
	rts.NumRenderTargets = 2;
	rts.RTFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
	rts.RTFormats[1] = DXGI_FORMAT_B8G8R8A8_UNORM;
	compushady_d3d12_append_subobject_to_stream(pipeline_stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, rts);
#endif

	if (py_rasterizer->py_dsv)
	{
		d3d12_Resource *py_resource_dsv = (d3d12_Resource *)py_rasterizer->py_dsv;
		compushady_d3d12_append_subobject_to_stream(pipeline_stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, py_resource_dsv->format);
	}

	D3D12_RASTERIZER_DESC rasterizer_desc = {};
	rasterizer_desc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizer_desc.CullMode = D3D12_CULL_MODE_BACK;
	rasterizer_desc.FrontCounterClockwise = false;

	compushady_d3d12_append_subobject_to_stream(pipeline_stream, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, rasterizer_desc);

	D3D12_PIPELINE_STATE_STREAM_DESC stream_desc = {};
	stream_desc.pPipelineStateSubobjectStream = pipeline_stream.data();
	stream_desc.SizeInBytes = pipeline_stream.size();

	hr = device2->CreatePipelineState(&stream_desc, __uuidof(ID3D12PipelineState), (void **)&py_rasterizer->pipeline);

	if (vs_view.buf)
		PyBuffer_Release(&vs_view);
	if (ps_view.buf)
		PyBuffer_Release(&ps_view);
	if (ms_view.buf)
		PyBuffer_Release(&ms_view);

	if (hr != S_OK)
	{
		Py_DECREF(py_rasterizer);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to create Rasterizer Pipeline State");
	}

	return (PyObject *)py_rasterizer;
}

static PyMethodDef d3d12_Device_methods[] = {
	{"create_buffer", (PyCFunction)d3d12_Device_create_buffer, METH_VARARGS, "Creates a Buffer object"},
	{"create_texture1d", (PyCFunction)d3d12_Device_create_texture1d, METH_VARARGS, "Creates a Texture1D object"},
	{"create_texture2d", (PyCFunction)d3d12_Device_create_texture2d, METH_VARARGS, "Creates a Texture2D object"},
	{"create_texture3d", (PyCFunction)d3d12_Device_create_texture3d, METH_VARARGS, "Creates a Texture3D object"},
	{"get_debug_messages", (PyCFunction)d3d12_Device_get_debug_messages, METH_VARARGS, "Get Device's debug messages"},
	{"create_compute", (PyCFunction)d3d12_Device_create_compute, METH_VARARGS | METH_KEYWORDS, "Creates a Compute Pipeline object"},
	{"create_rasterizer", (PyCFunction)d3d12_Device_create_rasterizer, METH_VARARGS | METH_KEYWORDS, "Creates a Rasterizer Pipeline object"},
	{"create_swapchain", (PyCFunction)d3d12_Device_create_swapchain, METH_VARARGS, "Creates a Swapchain object"},
	{"create_sampler", (PyCFunction)d3d12_Device_create_sampler, METH_VARARGS, "Creates a Sampler object"},
	{"create_heap", (PyCFunction)d3d12_Device_create_heap, METH_VARARGS, "Creates a Heap object"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject *d3d12_Resource_upload(d3d12_Resource *self, PyObject *args)
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

	char *mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void **)&mapped_data);
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

static PyObject *d3d12_Resource_upload2d(d3d12_Resource *self, PyObject *args)
{
	Py_buffer view;
	UINT pitch;
	UINT width;
	UINT height;
	UINT bytes_per_pixel;
	if (!PyArg_ParseTuple(args, "y*IIII", &view, &pitch, &width, &height, &bytes_per_pixel))
		return NULL;

	char *mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void **)&mapped_data);
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
		memcpy(mapped_data + (pitch * y), (char *)view.buf + offset, amount);
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

static PyObject *d3d12_Resource_upload_chunked(d3d12_Resource *self, PyObject *args)
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

	char *mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void **)&mapped_data);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		PyBuffer_Release(&filler);
		return d3d_generate_exception(PyExc_Exception, hr, "Unable to Map() ID3D12Resource1");
	}

	size_t offset = 0;
	for (uint32_t i = 0; i < elements; i++)
	{
		memcpy(mapped_data + offset, (char *)view.buf + (i * stride), stride);
		offset += stride;
		memcpy(mapped_data + offset, (char *)filler.buf, filler.len);
		offset += filler.len;
	}

	self->resource->Unmap(0, NULL);
	PyBuffer_Release(&view);
	PyBuffer_Release(&filler);
	Py_RETURN_NONE;
}

static PyObject *d3d12_Resource_readback(d3d12_Resource *self, PyObject *args)
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

	char *mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void **)&mapped_data);
	if (hr != S_OK)
	{
		return d3d_generate_exception(PyExc_Exception, hr, "Unable Map() ID3D12Resource1");
	}

	PyObject *py_bytes = PyBytes_FromStringAndSize(mapped_data + offset, size);
	self->resource->Unmap(0, NULL);
	return py_bytes;
}

static PyObject *d3d12_Resource_readback2d(d3d12_Resource *self, PyObject *args)
{
	UINT pitch;
	UINT width;
	UINT height;
	UINT bytes_per_pixel;
	if (!PyArg_ParseTuple(args, "IIII", &pitch, &width, &height, &bytes_per_pixel))
		return NULL;

	if (compushady_get_size_by_pitch(pitch, width, height, 1, bytes_per_pixel) > self->size)
	{
		return PyErr_Format(PyExc_ValueError, "requested buffer out of bounds: %llu (expected no more than %llu)", pitch * height, self->size);
	}

	char *mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void **)&mapped_data);
	if (hr != S_OK)
	{
		return d3d_generate_exception(PyExc_Exception, hr, "Unable Map() ID3D12Resource1");
	}

	char *data2d = (char *)PyMem_Malloc(width * height * bytes_per_pixel);
	if (!data2d)
	{
		self->resource->Unmap(0, NULL);
		return PyErr_Format(PyExc_MemoryError, "Unable to allocate memory for 2d data");
	}

	for (uint32_t y = 0; y < height; y++)
	{
		memcpy(data2d + (width * bytes_per_pixel * y), mapped_data + (pitch * y), width * bytes_per_pixel);
	}

	PyObject *py_bytes = PyBytes_FromStringAndSize(data2d, width * height * bytes_per_pixel);

	PyMem_Free(data2d);
	self->resource->Unmap(0, NULL);
	return py_bytes;
}

static PyObject *d3d12_Resource_readback_to_buffer(d3d12_Resource *self, PyObject *args)
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

	char *mapped_data;
	HRESULT hr = self->resource->Map(0, NULL, (void **)&mapped_data);
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

static PyObject *d3d12_Resource_copy_to(d3d12_Resource *self, PyObject *args)
{
	PyObject *py_destination;
	UINT64 size;
	UINT64 src_offset;
	UINT64 dst_offset;
	UINT width;
	UINT height;
	UINT depth;
	UINT src_x;
	UINT src_y;
	UINT src_z;
	UINT dst_x;
	UINT dst_y;
	UINT dst_z;
	UINT src_slice;
	UINT dst_slice;
	if (!PyArg_ParseTuple(args, "OKKKIIIIIIIIIII", &py_destination, &size, &src_offset, &dst_offset, &width, &height, &depth, &src_x, &src_y, &src_z, &dst_x, &dst_y, &dst_z, &src_slice, &dst_slice))
		return NULL;

	int ret = PyObject_IsInstance(py_destination, (PyObject *)&d3d12_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}

	d3d12_Resource *dst_resource = (d3d12_Resource *)py_destination;

	if (size == 0)
	{
		size = self->size;
	}

	bool texture_to_texture_copy = (self->dimension != D3D12_RESOURCE_DIMENSION_BUFFER) && (dst_resource->dimension != D3D12_RESOURCE_DIMENSION_BUFFER);
	if (!compushady_check_copy_to(self->dimension == D3D12_RESOURCE_DIMENSION_BUFFER,
								  dst_resource->dimension == D3D12_RESOURCE_DIMENSION_BUFFER, size, src_offset, dst_offset, self->size, dst_resource->size,
								  src_x, src_y, src_z, src_slice, self->slices, dst_slice, dst_resource->slices,
								  self->footprint.Footprint.Width, self->footprint.Footprint.Height, self->footprint.Footprint.Depth,
								  dst_resource->footprint.Footprint.Width, dst_resource->footprint.Footprint.Height, dst_resource->footprint.Footprint.Depth,
								  &dst_x, &dst_y, &dst_z, &width, &height, &depth))
	{
		return NULL;
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
		if (self->heap_type == COMPUSHADY_HEAP_DEFAULT)
		{
			barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
			self->py_device->command_list->ResourceBarrier(1, &barriers[0]);
			reset_barrier0 = true;
		}
		if (dst_resource->heap_type == COMPUSHADY_HEAP_DEFAULT)
		{
			barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			self->py_device->command_list->ResourceBarrier(1, &barriers[1]);
			reset_barrier1 = true;
		}
		self->py_device->command_list->CopyBufferRegion(dst_resource->resource, dst_offset, self->resource, src_offset, size);
	}
	else // texture copy
	{
		D3D12_TEXTURE_COPY_LOCATION dest_location = {};
		dest_location.pResource = dst_resource->resource;
		if (dst_resource->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		{
			dest_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			dest_location.PlacedFootprint = self->footprint;
			dest_location.PlacedFootprint.Offset = dst_offset;
		}
		else
		{
			barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			barriers[1].Transition.Subresource = dst_slice;
			self->py_device->command_list->ResourceBarrier(1, &barriers[1]);
			dest_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dest_location.SubresourceIndex = dst_slice;
			reset_barrier1 = true;
		}

		D3D12_TEXTURE_COPY_LOCATION src_location = {};
		src_location.pResource = self->resource;
		if (self->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
		{
			src_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src_location.PlacedFootprint = dst_resource->footprint;
			src_location.PlacedFootprint.Offset = src_offset;
		}
		else
		{
			barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
			barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
			barriers[0].Transition.Subresource = src_slice;
			self->py_device->command_list->ResourceBarrier(1, &barriers[0]);
			src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			src_location.SubresourceIndex = src_slice;
			reset_barrier0 = true;
		}

		D3D12_BOX box = {};
		box.left = src_x;
		box.right = src_x + width;
		box.top = src_y;
		box.bottom = src_y + height;
		box.front = src_z;
		box.back = src_z + depth;
		self->py_device->command_list->CopyTextureRegion(&dest_location, dst_x, dst_y, dst_z, &src_location, texture_to_texture_copy ? &box : NULL);
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

	self->py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList **)&self->py_device->command_list);
	self->py_device->queue->Signal(self->py_device->fence, ++self->py_device->fence_value);
	self->py_device->fence->SetEventOnCompletion(self->py_device->fence_value, self->py_device->fence_event);
	Py_BEGIN_ALLOW_THREADS;
	WaitForSingleObject(self->py_device->fence_event, INFINITE);
	Py_END_ALLOW_THREADS;

	Py_RETURN_NONE;
}

static PyObject *d3d12_Resource_bind_tile(d3d12_Resource *self, PyObject *args)
{
	UINT x;
	UINT y;
	UINT z;
	PyObject *py_heap;
	UINT64 heap_offset;
	UINT slice;
	if (!PyArg_ParseTuple(args, "IIIOKI", &x, &y, &z, &py_heap, &heap_offset, &slice))
		return NULL;

	if (self->tiles_x == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Not a sparsed Resource");
	}

	if (x >= self->tiles_x || y >= self->tiles_y || z >= self->tiles_z)
	{
		return PyErr_Format(PyExc_ValueError,
							"Tile (%u, %u, %u) is out of bounds "
							"(tiles_x: %u, tiles_y: %u, tiles_z: %u)",
							x, y, z, self->tiles_x, self->tiles_y, self->tiles_z);
	}

	ID3D12Heap *heap = NULL;
	if (py_heap && py_heap != Py_None)
	{
		int ret = PyObject_IsInstance(py_heap, (PyObject *)&d3d12_Heap_Type);
		if (ret < 0)
		{
			return NULL;
		}
		else if (ret == 0)
		{
			return PyErr_Format(PyExc_ValueError, "Expected a Heap object");
		}

		d3d12_Heap *py_d3d12_heap = (d3d12_Heap *)py_heap;

		if (py_d3d12_heap->py_device != self->py_device)
		{
			return PyErr_Format(PyExc_ValueError, "Cannot use heap from a different device");
		}

		if (py_d3d12_heap->heap_type != self->heap_type)
		{
			return PyErr_Format(Compushady_BufferError, "Unsupported heap type");
		}

		if (heap_offset >= py_d3d12_heap->size)
		{
			return PyErr_Format(Compushady_BufferError,
								"Invalid heap offset (%llu) "
								"(heap size %llu)",
								heap_offset, py_d3d12_heap->size);
		}

		heap = py_d3d12_heap->heap;
	}

	Py_XDECREF(self->py_heap);
	self->py_heap = heap ? (d3d12_Heap *)py_heap : NULL;
	Py_XINCREF(self->py_heap);

	D3D12_TILED_RESOURCE_COORDINATE tile_coordinate = {};
	tile_coordinate.X = x;
	tile_coordinate.Y = y;
	tile_coordinate.Z = z;
	tile_coordinate.Subresource = slice;

	D3D12_TILE_REGION_SIZE tile_region_size = {};
	tile_region_size.NumTiles = 1;

	const D3D12_TILE_RANGE_FLAGS tile_range_flags = heap ? D3D12_TILE_RANGE_FLAG_NONE : D3D12_TILE_RANGE_FLAG_NULL;

	const UINT tile_counts = 1;
	const UINT offset = heap ? (UINT)heap_offset : 0;

	self->py_device->queue->UpdateTileMappings(self->resource, 1, &tile_coordinate, &tile_region_size, heap, 1, &tile_range_flags, &offset, &tile_counts, D3D12_TILE_MAPPING_FLAG_NONE);
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
	{"bind_tile", (PyCFunction)d3d12_Resource_bind_tile, METH_VARARGS, "Bind a sparse resource tile to a heap"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject *d3d12_Pipeline_dispatch(d3d12_Pipeline *self, PyObject *args)
{
	UINT x, y, z;
	Py_buffer view;
	if (!PyArg_ParseTuple(args, "IIIy*:dispatch", &x, &y, &z, &view))
		return NULL;

	if (view.len > 0)
	{
		if (view.len > (self->push_constant_values * 4) || (view.len % 4) != 0)
		{
			return PyErr_Format(PyExc_ValueError,
								"Invalid push constant size: %u, expected max %u with 4 bytes alignment", view.len, (self->push_constant_values * 4));
		}
	}

	self->py_device->command_allocator->Reset();
	self->py_device->command_list->Reset(self->py_device->command_allocator, self->pipeline);

	if (self->descriptor_heaps[0])
	{
		self->py_device->command_list->SetDescriptorHeaps(self->descriptor_heaps[1] ? 2 : 1, self->descriptor_heaps);
	}
	else if (self->descriptor_heaps[1])
	{
		self->py_device->command_list->SetDescriptorHeaps(1, &self->descriptor_heaps[1]);
	}

	self->py_device->command_list->SetComputeRootSignature(self->root_signature);

	if (self->descriptor_heaps[0])
	{
		self->py_device->command_list->SetComputeRootDescriptorTable(0, self->descriptor_heaps[0]->GetGPUDescriptorHandleForHeapStart());
	}

	if (self->descriptor_heaps[1])
	{
		if (!self->descriptor_heaps[0])
		{
			self->py_device->command_list->SetComputeRootDescriptorTable(0, self->descriptor_heaps[1]->GetGPUDescriptorHandleForHeapStart());
		}
		else
		{
			self->py_device->command_list->SetComputeRootDescriptorTable(1, self->descriptor_heaps[1]->GetGPUDescriptorHandleForHeapStart());
		}
	}

	if (view.len > 0)
	{
		self->py_device->command_list->SetComputeRoot32BitConstants(self->push_constant_slot, (UINT)(view.len / 4), view.buf, 0);
	}

	self->py_device->command_list->Dispatch(x, y, z);
	self->py_device->command_list->Close();

	self->py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList **)&self->py_device->command_list);
	self->py_device->queue->Signal(self->py_device->fence, ++self->py_device->fence_value);
	self->py_device->fence->SetEventOnCompletion(self->py_device->fence_value, self->py_device->fence_event);
	Py_BEGIN_ALLOW_THREADS;
	WaitForSingleObject(self->py_device->fence_event, INFINITE);
	Py_END_ALLOW_THREADS;

	Py_RETURN_NONE;
}

static PyObject *d3d12_Pipeline_dispatch_indirect(d3d12_Pipeline *self, PyObject *args)
{
	PyObject *py_indirect_buffer;
	UINT offset;
	Py_buffer view;
	if (!PyArg_ParseTuple(args, "OIy*:dispatch_indirect", &py_indirect_buffer, &offset, &view))
		return NULL;

	int ret = PyObject_IsInstance(py_indirect_buffer, (PyObject *)&d3d12_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}

	d3d12_Resource *py_resource = (d3d12_Resource *)py_indirect_buffer;
	if (py_resource->dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Buffer object");
	}

	self->py_device->command_allocator->Reset();
	self->py_device->command_list->Reset(self->py_device->command_allocator, self->pipeline);
	if (self->descriptor_heaps[0])
	{
		self->py_device->command_list->SetDescriptorHeaps(self->descriptor_heaps[1] ? 2 : 1, self->descriptor_heaps);
	}
	else if (self->descriptor_heaps[1])
	{
		self->py_device->command_list->SetDescriptorHeaps(1, &self->descriptor_heaps[1]);
	}

	self->py_device->command_list->SetComputeRootSignature(self->root_signature);

	if (self->descriptor_heaps[0])
	{
		self->py_device->command_list->SetComputeRootDescriptorTable(0, self->descriptor_heaps[0]->GetGPUDescriptorHandleForHeapStart());
	}

	if (self->descriptor_heaps[1])
	{
		if (!self->descriptor_heaps[0])
		{
			self->py_device->command_list->SetComputeRootDescriptorTable(0, self->descriptor_heaps[1]->GetGPUDescriptorHandleForHeapStart());
		}
		else
		{
			self->py_device->command_list->SetComputeRootDescriptorTable(1, self->descriptor_heaps[1]->GetGPUDescriptorHandleForHeapStart());
		}
	}

	if (view.len > 0)
	{
		self->py_device->command_list->SetComputeRoot32BitConstants(self->push_constant_slot, (UINT)(view.len / 4), view.buf, 0);
	}

	self->py_device->command_list->ExecuteIndirect(self->command_signature, 1, py_resource->resource, offset, NULL, 0);
	self->py_device->command_list->Close();

	self->py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList **)&self->py_device->command_list);
	self->py_device->queue->Signal(self->py_device->fence, ++self->py_device->fence_value);
	self->py_device->fence->SetEventOnCompletion(self->py_device->fence_value, self->py_device->fence_event);
	Py_BEGIN_ALLOW_THREADS;
	WaitForSingleObject(self->py_device->fence_event, INFINITE);
	Py_END_ALLOW_THREADS;

	Py_RETURN_NONE;
}

static PyObject *d3d12_Pipeline_draw(d3d12_Pipeline *self, PyObject *args)
{
	UINT num_vertices, num_instances, start_vertex, start_instance;
	Py_buffer view;
	if (!PyArg_ParseTuple(args, "IIIIy*:draw", &num_vertices, &num_instances, &start_vertex, &start_instance, &view))
		return NULL;

	if (view.len > 0)
	{
		if (view.len > (self->push_constant_values * 4) || (view.len % 4) != 0)
		{
			return PyErr_Format(PyExc_ValueError,
								"Invalid push constant size: %u, expected max %u with 4 bytes alignment", view.len, (self->push_constant_values * 4));
		}
	}

	self->py_device->command_allocator->Reset();
	self->py_device->command_list->Reset(self->py_device->command_allocator, self->pipeline);

	if (self->descriptor_heaps[0])
	{
		self->py_device->command_list->SetDescriptorHeaps(self->descriptor_heaps[1] ? 2 : 1, self->descriptor_heaps);
	}
	else if (self->descriptor_heaps[1])
	{
		self->py_device->command_list->SetDescriptorHeaps(1, &self->descriptor_heaps[1]);
	}

	self->py_device->command_list->SetGraphicsRootSignature(self->root_signature);

	if (self->descriptor_heaps[0])
	{
		self->py_device->command_list->SetGraphicsRootDescriptorTable(0, self->descriptor_heaps[0]->GetGPUDescriptorHandleForHeapStart());
	}

	if (self->descriptor_heaps[1])
	{
		if (!self->descriptor_heaps[0])
		{
			self->py_device->command_list->SetGraphicsRootDescriptorTable(0, self->descriptor_heaps[1]->GetGPUDescriptorHandleForHeapStart());
		}
		else
		{
			self->py_device->command_list->SetGraphicsRootDescriptorTable(1, self->descriptor_heaps[1]->GetGPUDescriptorHandleForHeapStart());
		}
	}

	if (view.len > 0)
	{
		self->py_device->command_list->SetGraphicsRoot32BitConstants(self->push_constant_slot, (UINT)(view.len / 4), view.buf, 0);
	}

	self->py_device->command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = {};
	D3D12_RESOURCE_BARRIER barrier = {};
	if (self->dsv_descriptor_heap)
	{
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = ((d3d12_Resource *)self->py_dsv)->resource;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		self->py_device->command_list->ResourceBarrier(1, &barrier);
		dsv_handle = self->dsv_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
		self->py_device->command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
	}
	self->py_device->command_list->OMSetRenderTargets(0, NULL, false, self->dsv_descriptor_heap ? &dsv_handle : NULL);

	D3D12_VIEWPORT viewport = {};
	viewport.Width = 1024;
	viewport.Height = 1024;
	viewport.MaxDepth = 1;
	self->py_device->command_list->RSSetViewports(1, &viewport);

	D3D12_RECT scissor = {};
	scissor.right = 1024;
	scissor.bottom = 1024;
	self->py_device->command_list->RSSetScissorRects(1, &scissor);
	self->py_device->command_list->DrawInstanced(num_vertices, num_instances, start_vertex, start_instance);
	if (self->dsv_descriptor_heap)
	{
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
		self->py_device->command_list->ResourceBarrier(1, &barrier);
	}
	self->py_device->command_list->Close();

	self->py_device->queue->ExecuteCommandLists(1, (ID3D12CommandList **)&self->py_device->command_list);
	self->py_device->queue->Signal(self->py_device->fence, ++self->py_device->fence_value);
	self->py_device->fence->SetEventOnCompletion(self->py_device->fence_value, self->py_device->fence_event);
	Py_BEGIN_ALLOW_THREADS;
	WaitForSingleObject(self->py_device->fence_event, INFINITE);
	Py_END_ALLOW_THREADS;

	Py_RETURN_NONE;
}

static PyObject *d3d12_Pipeline_bind_cbv(d3d12_Pipeline *self, PyObject *args)
{
	UINT index;
	PyObject *py_resource;
	if (!PyArg_ParseTuple(args, "IO", &index, &py_resource))
		return NULL;

	if (self->bindless == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Compute pipeline is not in bindless mode");
	}

	if (index >= self->bindless)
	{
		return PyErr_Format(PyExc_ValueError, "Invalid bind index %u (max: %u)", index, self->bindless - 1);
	}

	const int ret = PyObject_IsInstance(py_resource, (PyObject *)&d3d12_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}

	d3d12_Resource *py_cbv = (d3d12_Resource *)py_resource;

	if (py_cbv->dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Buffer object");
	}

	D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = self->bind_start;
	cpu_handle.ptr += index * self->bind_increment;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
	cbv_desc.BufferLocation = py_cbv->resource->GetGPUVirtualAddress();
	cbv_desc.SizeInBytes = (UINT)COMPUSHADY_ALIGN(py_cbv->size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	self->py_device->device->CreateConstantBufferView(&cbv_desc, cpu_handle);

	Py_INCREF(py_resource);
	PyList_SetItem(self->py_cbv_list, index, py_resource);

	Py_RETURN_NONE;
}

static PyObject *d3d12_Pipeline_bind_srv(d3d12_Pipeline *self, PyObject *args)
{
	UINT index;
	PyObject *py_resource;
	if (!PyArg_ParseTuple(args, "IO", &index, &py_resource))
		return NULL;

	if (self->bindless == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Compute pipeline is not in bindless mode");
	}

	if (index >= self->bindless)
	{
		return PyErr_Format(PyExc_ValueError, "Invalid bind index %u (max: %u)", index, self->bindless - 1);
	}

	const int ret = PyObject_IsInstance(py_resource, (PyObject *)&d3d12_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}

	d3d12_Resource *py_srv = (d3d12_Resource *)py_resource;

	D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = self->bind_start;
	cpu_handle.ptr += (self->bindless * self->bind_increment) + index * self->bind_increment;

	if (py_srv->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
		srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv_desc.Format = py_srv->format;
		srv_desc.Buffer.NumElements = (UINT)(py_srv->size / (srv_desc.Format ? dxgi_pixels_sizes[py_srv->format] : 1));
		srv_desc.Buffer.StructureByteStride = py_srv->stride;
		if (py_srv->stride > 0)
		{
			srv_desc.Buffer.NumElements = (UINT)(py_srv->size / py_srv->stride);
		}
		self->py_device->device->CreateShaderResourceView(py_srv->resource, &srv_desc, cpu_handle);
	}
	else
	{
		self->py_device->device->CreateShaderResourceView(py_srv->resource, NULL, cpu_handle);
	}

	Py_INCREF(py_resource);
	PyList_SetItem(self->py_srv_list, index, py_resource);

	Py_RETURN_NONE;
}

static PyObject *d3d12_Pipeline_bind_uav(d3d12_Pipeline *self, PyObject *args)
{
	UINT index;
	PyObject *py_resource;
	if (!PyArg_ParseTuple(args, "IO", &index, &py_resource))
		return NULL;

	if (self->bindless == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Compute pipeline is not in bindless mode");
	}

	if (index >= self->bindless)
	{
		return PyErr_Format(PyExc_ValueError, "Invalid bind index %u (max: %u)", index, self->bindless - 1);
	}

	const int ret = PyObject_IsInstance(py_resource, (PyObject *)&d3d12_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}

	d3d12_Resource *py_uav = (d3d12_Resource *)py_resource;

	D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = self->bind_start;
	cpu_handle.ptr += (self->bindless * self->bind_increment * 2) + index * self->bind_increment;

	if (py_uav->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uav_desc.Format = py_uav->format;
		uav_desc.Buffer.NumElements = (UINT)(py_uav->size / (uav_desc.Format ? dxgi_pixels_sizes[py_uav->format] : 1));
		uav_desc.Buffer.StructureByteStride = py_uav->stride;
		if (py_uav->stride > 0)
		{
			uav_desc.Buffer.NumElements = (UINT)(py_uav->size / py_uav->stride);
		}
		self->py_device->device->CreateUnorderedAccessView(py_uav->resource, NULL, &uav_desc, cpu_handle);
	}
	else
	{
		self->py_device->device->CreateUnorderedAccessView(py_uav->resource, NULL, NULL, cpu_handle);
	}

	Py_INCREF(py_resource);
	PyList_SetItem(self->py_uav_list, index, py_resource);

	Py_RETURN_NONE;
}

static PyMethodDef d3d12_Pipeline_methods[] = {
	{"dispatch", (PyCFunction)d3d12_Pipeline_dispatch, METH_VARARGS, "Execute a Compute Pipeline"},
	{"dispatch_indirect", (PyCFunction)d3d12_Pipeline_dispatch_indirect, METH_VARARGS, "Execute an Indirect Compute Pipeline"},
	{"draw", (PyCFunction)d3d12_Pipeline_draw, METH_VARARGS, "Execute a Rasterizer Pipeline"},
	{"bind_cbv", (PyCFunction)d3d12_Pipeline_bind_cbv, METH_VARARGS, "Bind a CBV to a Bindless Pipeline"},
	{"bind_srv", (PyCFunction)d3d12_Pipeline_bind_srv, METH_VARARGS, "Bind an SRV to a Bindless Pipeline"},
	{"bind_uav", (PyCFunction)d3d12_Pipeline_bind_uav, METH_VARARGS, "Bind an UAV to a Bindless Pipeline"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject *d3d12_get_discovered_devices(PyObject *self)
{
	IDXGIFactory1 *factory = NULL;
	HRESULT hr = CreateDXGIFactory2(d3d12_debug ? DXGI_CREATE_FACTORY_DEBUG : 0, __uuidof(IDXGIFactory1), (void **)&factory);
	if (hr != S_OK)
	{
		return d3d_generate_exception(PyExc_Exception, hr, "unable to create IDXGIFactory1");
	}

	PyObject *py_list = PyList_New(0);

	UINT i = 0;
	IDXGIAdapter1 *adapter;
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

		d3d12_Device *py_device = (d3d12_Device *)PyObject_New(d3d12_Device, &d3d12_Device_Type);
		if (!py_device)
		{
			factory->Release();
			Py_DECREF(py_list);
			return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Device");
		}
		COMPUSHADY_CLEAR(py_device);

		py_device->adapter = adapter; // keep the com refcnt as is given that EnumAdapters1 increases it
		py_device->device = NULL;	  // will be lazily allocated
		py_device->name = PyUnicode_FromWideChar(adapter_desc.Description, -1);
		py_device->dedicated_video_memory = adapter_desc.DedicatedVideoMemory;
		py_device->dedicated_system_memory = adapter_desc.DedicatedSystemMemory;
		py_device->shared_system_memory = adapter_desc.SharedSystemMemory;
		py_device->vendor_id = adapter_desc.VendorId;
		py_device->device_id = adapter_desc.DeviceId;
		py_device->is_hardware = (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0;
		py_device->is_discrete = py_device->is_hardware;

		PyList_Append(py_list, (PyObject *)py_device);
		Py_DECREF(py_device);
	}

	factory->Release();

	return py_list;
}

static PyObject *d3d12_enable_debug(PyObject *self)
{
	ID3D12Debug *debug;
	D3D12GetDebugInterface(__uuidof(ID3D12Debug), (void **)&debug);
	debug->EnableDebugLayer();
	Py_RETURN_NONE;
}

static PyObject *d3d12_get_shader_binary_type(PyObject *self)
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
	compushady_backends_d3d12_methods};

PyMODINIT_FUNC
PyInit_d3d12(void)
{
	PyObject *m = compushady_backend_init(
		&compushady_backends_d3d12_module,
		&d3d12_Device_Type, d3d12_Device_members, d3d12_Device_methods,
		&d3d12_Resource_Type, d3d12_Resource_members, d3d12_Resource_methods,
		&d3d12_Swapchain_Type, /*d3d12_Swapchain_members*/ NULL, d3d12_Swapchain_methods,
		&d3d12_Pipeline_Type, NULL, d3d12_Pipeline_methods,
		&d3d12_Sampler_Type, NULL, NULL,
		&d3d12_Heap_Type, d3d12_Heap_members, NULL);

	dxgi_init_pixel_formats();

	return m;
}

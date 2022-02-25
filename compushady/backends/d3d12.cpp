
#include <d3d12.h>
#include <dxgi1_3.h>
#include <comdef.h>
#include <vector>
#include <unordered_map>

#include "compushady.h"

std::unordered_map<int, size_t> dxgi_pixels_sizes;

#define DXGI_PIXEL_SIZE(x, value) dxgi_pixels_sizes[x] = value

typedef struct d3d12_Device
{
	PyObject_HEAD;
	IDXGIAdapter1* adapter;
	ID3D12Device1* device;
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
	D3D12_RESOURCE_DIMENSION dimension;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
} d3d12_Resource;

typedef struct d3d12_Swapchain
{
	PyObject_HEAD;
	d3d12_Device* py_device;
} d3d12_Swapchain;

typedef struct d3d12_Compute
{
	PyObject_HEAD;
	d3d12_Device* py_device;
	ID3D12RootSignature* root_signature;
	ID3D12DescriptorHeap* descriptor_heap;
	ID3D12PipelineState* pipeline;
	ID3D12CommandQueue* queue;
	ID3D12Fence1* fence;
	HANDLE fence_event;
	UINT64 fence_value;
	ID3D12CommandAllocator* command_allocator;
	ID3D12GraphicsCommandList1* command_list;
} d3d12_Compute;

static PyObject* d3d12_generate_exception(HRESULT hr, const char* prefix)
{
	_com_error err(hr);
	return PyErr_Format(PyExc_Exception, "%s: %s\n", prefix, err.ErrorMessage());
}

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

static ID3D12Device1* d3d12_Device_get_device(d3d12_Device* self)
{
	if (self->device)
		return self->device;

	HRESULT hr = D3D12CreateDevice(self->adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device1), (void**)&self->device);
	if (hr != S_OK)
	{
		d3d12_generate_exception(hr, "Unable to create ID3D12Device1");
		return NULL;
	}

	return self->device;
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
	Py_XDECREF(self->py_device);
	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject d3d12_Swapchain_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.d3d12.Swapchain", /* tp_name */
	sizeof(d3d12_Device),                                              /* tp_basicsize */
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
	if (self->command_list)
		self->command_list->Release();
	if (self->command_allocator)
		self->command_allocator->Release();
	if (self->fence_event)
		CloseHandle(self->fence_event);
	if (self->fence)
		self->fence->Release();
	if (self->queue)
		self->queue->Release();
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

	ID3D12Device1* device = d3d12_Device_get_device(self);
	if (!device)
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
	HRESULT hr = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC*)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);

	if (hr != S_OK)
	{
		return d3d12_generate_exception(hr, "Unable to create ID3D12Resource1");
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

	ID3D12Device1* device = d3d12_Device_get_device(self);
	if (!device)
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
	HRESULT hr = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC*)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);

	if (hr != S_OK)
	{
		return d3d12_generate_exception(hr, "Unable to create ID3D12Resource1");
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
	device->GetCopyableFootprints((D3D12_RESOURCE_DESC*)&resource_desc, 0, 1, 0, &py_resource->footprint, NULL, NULL, &py_resource->size);
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

	ID3D12Device1* device = d3d12_Device_get_device(self);
	if (!device)
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
	HRESULT hr = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC*)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);

	if (hr != S_OK)
	{
		return d3d12_generate_exception(hr, "Unable to create ID3D12Resource1");
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
	device->GetCopyableFootprints((D3D12_RESOURCE_DESC*)&resource_desc, 0, 1, 0, &py_resource->footprint, NULL, NULL, &py_resource->size);
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

	ID3D12Device1* device = d3d12_Device_get_device(self);
	if (!device)
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
	HRESULT hr = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, (D3D12_RESOURCE_DESC*)&resource_desc, state, NULL, __uuidof(ID3D12Resource1), (void**)&resource);

	if (hr != S_OK)
	{
		return d3d12_generate_exception(hr, "Unable to create ID3D12Resource1");
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

	device->GetCopyableFootprints((D3D12_RESOURCE_DESC*)&resource_desc, 0, 1, 0, &py_resource->footprint, NULL, NULL, &py_resource->size);
	py_resource->dimension = resource_desc.Dimension;
	py_resource->stride = 0;
	py_resource->format = format;

	return (PyObject*)py_resource;
}

static PyObject* d3d12_Device_create_texture2d_from_native(d3d12_Device* self, PyObject* args)
{
	unsigned long long texture_ptr;
	if (!PyArg_ParseTuple(args, "K", &texture_ptr))
		return NULL;

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
	py_resource->stride = 0;
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
			PyObject* py_debug_message = PyUnicode_FromStringAndSize(message->pDescription, message->DescriptionByteLength);
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

	ID3D12Device1* device = d3d12_Device_get_device(self);
	if (!device)
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
		return d3d12_generate_exception(hr, "Unable to serialize Versioned Root Signature");
	}

	d3d12_Compute* py_compute = (d3d12_Compute*)PyObject_New(d3d12_Compute, &d3d12_Compute_Type);
	if (!py_compute)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Compute");
	}
	COMPUSHADY_CLEAR(py_compute);
	py_compute->py_device = self;
	Py_INCREF(py_compute->py_device);

	hr = device->CreateRootSignature(0, serialized_root_signature->GetBufferPointer(), serialized_root_signature->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&py_compute->root_signature);
	serialized_root_signature->Release();
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		Py_DECREF(py_compute);
		return d3d12_generate_exception(hr, "Unable to create Root Signature");
	}

	D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = {};
	descriptor_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descriptor_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descriptor_heap_desc.NumDescriptors = (UINT)(cbv.size() + srv.size() + uav.size());

	hr = device->CreateDescriptorHeap(&descriptor_heap_desc, __uuidof(ID3D12DescriptorHeap), (void**)&py_compute->descriptor_heap);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		Py_DECREF(py_compute);
		return d3d12_generate_exception(hr, "Unable to create Descriptor Heap");
	}

	UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = py_compute->descriptor_heap->GetCPUDescriptorHandleForHeapStart();

	for (d3d12_Resource* resource : cbv)
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
		cbv_desc.BufferLocation = resource->resource->GetGPUVirtualAddress();
		cbv_desc.SizeInBytes = (UINT)COMPUSHADY_ALIGN(resource->size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		device->CreateConstantBufferView(&cbv_desc, cpu_handle);
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
			device->CreateShaderResourceView(resource->resource, &srv_desc, cpu_handle);
		}
		else
		{
			device->CreateShaderResourceView(resource->resource, NULL, cpu_handle);
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
			device->CreateUnorderedAccessView(resource->resource, NULL, &uav_desc, cpu_handle);
		}
		else
		{
			device->CreateUnorderedAccessView(resource->resource, NULL, NULL, cpu_handle);
		}
		cpu_handle.ptr += increment;
	}

	D3D12_COMPUTE_PIPELINE_STATE_DESC compute_pipeline_desc = {};
	compute_pipeline_desc.pRootSignature = py_compute->root_signature;
	compute_pipeline_desc.CS.pShaderBytecode = view.buf;
	compute_pipeline_desc.CS.BytecodeLength = view.len;

	hr = device->CreateComputePipelineState(&compute_pipeline_desc, __uuidof(ID3D12PipelineState), (void**)&py_compute->pipeline);
	if (hr != S_OK)
	{
		PyBuffer_Release(&view);
		Py_DECREF(py_compute);
		return d3d12_generate_exception(hr, "Unable to create Compute Pipeline State");
	}

	PyBuffer_Release(&view);

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;

	hr = self->device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), (void**)&py_compute->queue);
	if (hr != S_OK)
	{
		Py_DECREF(py_compute);
		return d3d12_generate_exception(hr, "Unable to create Command Queue");
	}

	hr = self->device->CreateFence(py_compute->fence_value, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence1), (void**)&py_compute->fence);
	if (hr != S_OK)
	{
		Py_DECREF(py_compute);
		return d3d12_generate_exception(hr, "Unable to create Fence");
	}

	py_compute->fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);

	hr = self->device->CreateCommandAllocator(queue_desc.Type, __uuidof(ID3D12CommandAllocator), (void**)&py_compute->command_allocator);
	if (hr != S_OK)
	{
		Py_DECREF(py_compute);
		return d3d12_generate_exception(hr, "Unable to create Command Allocator");
	}

	hr = self->device->CreateCommandList(0, queue_desc.Type, py_compute->command_allocator, NULL, __uuidof(ID3D12GraphicsCommandList1), (void**)&py_compute->command_list);
	if (hr != S_OK)
	{
		Py_DECREF(py_compute);
		return d3d12_generate_exception(hr, "Unable to create Command List");
	}

	py_compute->command_list->Close();

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
		return d3d12_generate_exception(hr, "Unable to Map() ID3D12Resource1");
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
		return d3d12_generate_exception(hr, "Unable to Map() ID3D12Resource1");
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
		return d3d12_generate_exception(hr, "Unable Map() ID3D12Resource1");
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
		return d3d12_generate_exception(hr, "Unable Map() ID3D12Resource1");
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
		return d3d12_generate_exception(hr, "Unable Map() ID3D12Resource1");
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

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
	ID3D12CommandQueue* queue;
	HRESULT hr = self->py_device->device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), (void**)&queue);
	if (hr != S_OK)
	{
		return d3d12_generate_exception(hr, "Unable to create Command Queue");
	}

	ID3D12Fence1* fence;
	hr = self->py_device->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence1), (void**)&fence);
	if (hr != S_OK)
	{
		queue->Release();
		return d3d12_generate_exception(hr, "Unable to create Fence");
	}

	HANDLE fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	fence->SetEventOnCompletion(1, fence_event);

	ID3D12CommandAllocator* command_allocator;
	hr = self->py_device->device->CreateCommandAllocator(queue_desc.Type, __uuidof(ID3D12CommandAllocator), (void**)&command_allocator);
	if (hr != S_OK)
	{
		fence->Release();
		CloseHandle(fence_event);
		queue->Release();
		return d3d12_generate_exception(hr, "Unable to create Command Allocator");
	}

	ID3D12GraphicsCommandList1* command_list;
	hr = self->py_device->device->CreateCommandList(0, queue_desc.Type, command_allocator, NULL, __uuidof(ID3D12GraphicsCommandList1), (void**)&command_list);
	if (hr != S_OK)
	{
		command_allocator->Release();
		fence->Release();
		CloseHandle(fence_event);
		queue->Release();
		return d3d12_generate_exception(hr, "Unable to create Command List");
	}

	if (self->dimension == D3D12_RESOURCE_DIMENSION_BUFFER && dst_resource->dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
	{
		command_list->CopyBufferRegion(dst_resource->resource, 0, self->resource, 0, self->size);
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
			dest_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
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
			src_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		}
		command_list->CopyTextureRegion(&dest_location, 0, 0, 0, &src_location, NULL);
	}
	command_list->Close();

	queue->ExecuteCommandLists(1, (ID3D12CommandList**)&command_list);
	queue->Signal(fence, 1);

	WaitForSingleObject(fence_event, INFINITE);

	command_list->Release();
	command_allocator->Release();
	fence->Release();
	CloseHandle(fence_event);
	queue->Release();

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

	self->command_list->Reset(self->command_allocator, self->pipeline);
	self->command_list->SetDescriptorHeaps(1, &self->descriptor_heap);
	self->command_list->SetComputeRootSignature(self->root_signature);
	self->command_list->SetComputeRootDescriptorTable(0, self->descriptor_heap->GetGPUDescriptorHandleForHeapStart());
	self->command_list->Dispatch(x, y, z);
	self->command_list->Close();

	self->queue->ExecuteCommandLists(1, (ID3D12CommandList**)&self->command_list);
	self->queue->Signal(self->fence, ++self->fence_value);
	self->fence->SetEventOnCompletion(self->fence_value, self->fence_event);
	WaitForSingleObject(self->fence_event, INFINITE);

	Py_RETURN_NONE;
}

static PyMethodDef d3d12_Compute_methods[] = {
	{"dispatch", (PyCFunction)d3d12_Compute_dispatch, METH_VARARGS, "Execute a Compute Pipeline"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* d3d12_get_discovered_devices(PyObject* self)
{
	IDXGIFactory1* factory = NULL;
	HRESULT hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory1), (void**)&factory);
	if (hr != S_OK)
	{
		_com_error err(hr);
		return PyErr_Format(PyExc_Exception, "unable to create IDXGIFactory1: %s", err.ErrorMessage());
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
			_com_error err(hr);
			return PyErr_Format(PyExc_Exception, "error while calling GetDesc1: %s", err.ErrorMessage());
		}

		d3d12_Device* py_device = (d3d12_Device*)PyObject_New(d3d12_Device, &d3d12_Device_Type);
		if (!py_device)
		{
			Py_DECREF(py_list);
			return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d12 Device");
		}

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
		&d3d12_Swapchain_Type, /*d3d12_Swapchain_members*/ NULL, /*d3d12_Swapchain_methods*/ NULL,
		&d3d12_Compute_Type, NULL, d3d12_Compute_methods
	);

	DXGI_PIXEL_SIZE(R32G32B32A32_FLOAT, 16);
	DXGI_PIXEL_SIZE(R32G32B32A32_UINT, 16);
	DXGI_PIXEL_SIZE(R32G32B32A32_SINT, 16);
	DXGI_PIXEL_SIZE(R32G32B32_FLOAT, 12);
	DXGI_PIXEL_SIZE(R32G32B32_UINT, 12);
	DXGI_PIXEL_SIZE(R32G32B32_SINT, 12);
	DXGI_PIXEL_SIZE(R16G16B16A16_FLOAT, 8);
	DXGI_PIXEL_SIZE(R16G16B16A16_UNORM, 8);
	DXGI_PIXEL_SIZE(R16G16B16A16_UINT, 8);
	DXGI_PIXEL_SIZE(R16G16B16A16_SNORM, 8);
	DXGI_PIXEL_SIZE(R16G16B16A16_SINT, 8);
	DXGI_PIXEL_SIZE(R32G32_FLOAT, 8);
	DXGI_PIXEL_SIZE(R32G32_UINT, 8);
	DXGI_PIXEL_SIZE(R32G32_SINT, 8);
	DXGI_PIXEL_SIZE(R8G8B8A8_UNORM, 4);
	DXGI_PIXEL_SIZE(R8G8B8A8_UNORM_SRGB, 4);
	DXGI_PIXEL_SIZE(R8G8B8A8_UINT, 4);
	DXGI_PIXEL_SIZE(R8G8B8A8_SNORM, 4);
	DXGI_PIXEL_SIZE(R8G8B8A8_SINT, 4);
	DXGI_PIXEL_SIZE(R16G16_FLOAT, 4);
	DXGI_PIXEL_SIZE(R16G16_UNORM, 4);
	DXGI_PIXEL_SIZE(R16G16_UINT, 4);
	DXGI_PIXEL_SIZE(R16G16_SNORM, 4);
	DXGI_PIXEL_SIZE(R16G16_SINT, 4);
	DXGI_PIXEL_SIZE(R32_FLOAT, 4);
	DXGI_PIXEL_SIZE(R32_UINT, 4);
	DXGI_PIXEL_SIZE(R32_SINT, 4);
	DXGI_PIXEL_SIZE(R8G8_UNORM, 2);
	DXGI_PIXEL_SIZE(R8G8_UINT, 2);
	DXGI_PIXEL_SIZE(R8G8_SNORM, 2);
	DXGI_PIXEL_SIZE(R8G8_SINT, 2);
	DXGI_PIXEL_SIZE(R16_FLOAT, 2);
	DXGI_PIXEL_SIZE(R16_UNORM, 2);
	DXGI_PIXEL_SIZE(R16_UINT, 2);
	DXGI_PIXEL_SIZE(R16_SNORM, 2);
	DXGI_PIXEL_SIZE(R16_SINT, 2);
	DXGI_PIXEL_SIZE(R8_UNORM, 1);
	DXGI_PIXEL_SIZE(R8_UINT, 1);
	DXGI_PIXEL_SIZE(R8_SNORM, 1);
	DXGI_PIXEL_SIZE(R8_SINT, 1);
	DXGI_PIXEL_SIZE(B8G8R8A8_UNORM, 4);
	DXGI_PIXEL_SIZE(B8G8R8A8_UNORM_SRGB, 4);


	return m;
}
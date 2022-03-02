#include <d3d11.h>
#include <dxgi1_3.h>

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
} d3d11_Resource;

typedef struct d3d11_Swapchain
{
	PyObject_HEAD;
	d3d11_Device* py_device;
	IDXGISwapChain1* swapchain;
} d3d11_Swapchain;

typedef struct d3d11_Compute
{
	PyObject_HEAD;
	d3d11_Device* py_device;
} d3d11_Compute;

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

	d3d11_Device* py_device = d3d11_Device_get_device(self);
	if (!py_device)
		return NULL;

	D3D11_BUFFER_DESC buffer_desc = {};

	switch (heap)
	{
	case COMPUSHADY_HEAP_DEFAULT:
		buffer_desc.Usage = D3D11_USAGE_DEFAULT;
		buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		break;
	case COMPUSHADY_HEAP_UPLOAD:
		buffer_desc.Usage = D3D11_USAGE_STAGING;
		buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		break;
	case COMPUSHADY_HEAP_READBACK:
		buffer_desc.Usage = D3D11_USAGE_STAGING;
		buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		break;
	default:
		return PyErr_Format(PyExc_ValueError, "invalid heap type: %d", heap);
	}

	buffer_desc.ByteWidth = (UINT)size;
	buffer_desc.StructureByteStride = stride;

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
	const char* kwlist[] = { "shader", "cbv", "srv", "uav", NULL };
	Py_buffer view;
	PyObject* py_cbv = NULL;
	PyObject* py_srv = NULL;
	PyObject* py_uav = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*|OOO", (char**)kwlist,
		&view, &py_cbv, &py_srv, &py_uav))
		return NULL;

	d3d11_Device* py_device = d3d11_Device_get_device(self);
	if (!py_device)
		return NULL;

	if (py_cbv)
	{
		PyObject* py_iter = PyObject_GetIter(py_cbv);
		if (!py_iter)
			return NULL;
		while (PyObject* py_item = PyIter_Next(py_iter))
		{
			int ret = PyObject_IsInstance(py_item, (PyObject*)&d3d11_Resource_Type);
			if (ret < 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return NULL;
			}
			else if (ret == 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
			}
			//cbv.push_back((d3d12_Resource*)py_item);
			Py_DECREF(py_item);
		}
		Py_DECREF(py_iter);
	}

	if (py_srv)
	{
		PyObject* py_iter = PyObject_GetIter(py_srv);
		if (!py_iter)
			return NULL;
		while (PyObject* py_item = PyIter_Next(py_iter))
		{
			int ret = PyObject_IsInstance(py_item, (PyObject*)&d3d11_Resource_Type);
			if (ret < 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return NULL;
			}
			else if (ret == 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
			}
			//srv.push_back((d3d12_Resource*)py_item);
			Py_DECREF(py_item);
		}
		Py_DECREF(py_iter);
	}

	if (py_uav)
	{
		PyObject* py_iter = PyObject_GetIter(py_uav);
		if (!py_iter)
			return NULL;
		while (PyObject* py_item = PyIter_Next(py_iter))
		{
			int ret = PyObject_IsInstance(py_item, (PyObject*)&d3d11_Resource_Type);
			if (ret < 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return NULL;
			}
			else if (ret == 0)
			{
				Py_DECREF(py_item);
				Py_DECREF(py_iter);
				return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
			}
			//uav.push_back((d3d12_Resource*)py_item);
			Py_DECREF(py_item);
		}
		Py_DECREF(py_iter);
	}

	d3d11_Compute* py_compute = (d3d11_Compute*)PyObject_New(d3d11_Compute, &d3d11_Compute_Type);
	if (!py_compute)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate d3d11 Compute");
	}
	COMPUSHADY_CLEAR(py_compute);
	py_compute->py_device = py_device;
	Py_INCREF(py_compute->py_device);

	ID3D11ComputeShader* compute_shader;
	py_device->device->CreateComputeShader(view.buf, view.len, NULL, &compute_shader);

	self->context->CSSetShader(compute_shader, NULL, 0);
	//self->context->CSSetConstantBuffers(0, );
	return (PyObject*)py_compute;
}

static PyMethodDef d3d11_Device_methods[] = {
	{"create_buffer", (PyCFunction)d3d11_Device_create_buffer, METH_VARARGS, "Creates a Buffer object"},
	{"create_texture2d", (PyCFunction)d3d11_Device_create_texture2d, METH_VARARGS, "Creates a Texture2D object"},
	{"get_debug_messages", (PyCFunction)d3d11_Device_get_debug_messages, METH_VARARGS, "Get Device's debug messages"},
	{"create_compute", (PyCFunction)d3d11_Device_create_compute, METH_VARARGS | METH_KEYWORDS, "Creates a Compute object"},
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
			printf("DATA %x %x %x %x\n", ((char*)mapped.pData)[0], ((char*)mapped.pData)[1], ((char*)mapped.pData)[2], ((char*)mapped.pData)[3]);
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
				buffer_desc.ByteWidth = self->size;
				HRESULT hr = self->py_device->device->CreateBuffer(&buffer_desc, NULL, (ID3D11Buffer**)&self->staging_resource);
				if (hr != S_OK)
				{
					return PyErr_Format(PyExc_Exception, "unable to create the staging buffer");
				}
			}
			self->py_device->context->CopyResource(self->staging_resource, self->resource);
			self->py_device->context->Flush();
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
			D3D11_TEXTURE2D_DESC texture2d_desc = {};
			texture2d_desc.Width = self->width;
			texture2d_desc.Height = self->height;
			texture2d_desc.Usage = D3D11_USAGE_STAGING;
			texture2d_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			texture2d_desc.ArraySize = 1;
			texture2d_desc.MipLevels = 1;
			texture2d_desc.Format = DXGI_FORMAT_R8G8B8A8_UINT; // TODO fix it
			texture2d_desc.SampleDesc.Count = 1;

			HRESULT hr = self->py_device->device->CreateTexture2D(&texture2d_desc, NULL, (ID3D11Texture2D**)&self->staging_resource);
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
		self->py_device->context->UpdateSubresource(dst_resource->resource, 0, NULL, mapped.pData, self->row_pitch, self->row_pitch * self->height);
		self->py_device->context->Unmap(self->staging_resource, 0);
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

		py_device->adapter = adapter; // kepp the com refcnt as is given that EnumAdapters1 increases it
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
	return PyLong_FromLong(COMPUSHADY_SHADER_BINARY_TYPE_DXIL);
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
		&d3d11_Swapchain_Type, /*d3d11_Swapchain_members*/ NULL, /*d3d11_Swapchain_methods*/ NULL,
		&d3d11_Compute_Type, NULL, d3d11_Compute_methods
	);

	dxgi_init_pixel_formats();

	return m;
}
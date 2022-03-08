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

	return m;
}

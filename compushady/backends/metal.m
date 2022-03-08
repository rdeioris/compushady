#include "compushady.h"
#import <MetalKit/MetalKit.h>

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

static PyMethodDef compushady_backends_metal_methods[] = {
	{"create_metal_layer", (PyCFunction)compushady_create_metal_layer, METH_VARARGS, "Creates a CAMetalLayer on the default device"},
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
	return PyModule_Create(&compushady_backends_metal_module);
}

#include "compushady.h"

#include <comdef.h>

PyObject* d3d_generate_exception(PyObject* py_exc, HRESULT hr, const char* prefix)
{
	_com_error err(hr);
	return PyErr_Format(py_exc, "%s: %s\n", prefix, err.ErrorMessage());
}

std::unordered_map<int, size_t> dxgi_pixels_sizes;

#define DXGI_PIXEL_SIZE(x, value) dxgi_pixels_sizes[x] = value

void dxgi_init_pixel_formats()
{
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
}
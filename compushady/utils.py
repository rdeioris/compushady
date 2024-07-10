import platform
from compushady import Swapchain, Buffer, Texture2D, HEAP_DEFAULT, HEAP_UPLOAD
from compushady.formats import B8G8R8A8_UNORM, R8G8B8A8_UNORM


def create_swapchain_from_glfw(window, format=B8G8R8A8_UNORM, num_buffers=3):
    import glfw

    if platform.system() == "Windows":
        swapchain = Swapchain(glfw.get_win32_window(window), format, 2)
    elif platform.system() == "Darwin":
        from compushady.backends.metal import create_metal_layer

        ca_metal_layer = create_metal_layer(glfw.get_cocoa_window(window), format)
        swapchain = Swapchain(ca_metal_layer, format, num_buffers)
    else:
        swapchain = Swapchain(
            (glfw.get_x11_display(), glfw.get_x11_window(window)),
            format,
            num_buffers,
        )
    return swapchain


def load_numpy_to_buffer(data, format=0, stride=0):
    b0 = Buffer(size=data.nbytes, heap_type=HEAP_DEFAULT, format=format, stride=stride)
    b0_upload = Buffer(b0.size, HEAP_UPLOAD)
    b0_upload.upload(data.reshape(-1))
    b0_upload.copy_to(b0)
    return b0


def load_pil_to_texture2d(image, texture=None, slice=0, mip=0):
    from PIL import Image

    if not isinstance(image, Image.Image):
        image = Image.open(image)
    if texture is None:
        texture = Texture2D(
            image.size[0], image.size[1], format=R8G8B8A8_UNORM, mips=mip + 1
        )
    t0_upload = Buffer(texture.size, HEAP_UPLOAD)
    t0_upload.upload2d(
        image.convert("RGBA").tobytes(),
        texture.get_mip_row_pitch(mip),
        texture.width >> mip,
        texture.height >> mip,
        4,
    )
    t0_upload.copy_to(texture, dst_slice=slice, dst_mip=mip)
    return texture

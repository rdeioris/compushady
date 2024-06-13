import platform
from compushady import Swapchain
from compushady.formats import B8G8R8A8_UNORM


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


def load_image(filename):
    from PIL import Image

    rgba = Image.open(filename).convert("RGBA")
    return (rgba.tobytes(), *rgba.size)

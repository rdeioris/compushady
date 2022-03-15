import glfw
from compushady import HEAP_UPLOAD, Buffer, Swapchain, Texture2D
from compushady.formats import R8G8B8A8_UNORM, B8G8R8A8_UNORM
import platform
import random

glfw.init()
# we do not want implicit OpenGL!
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

target = Texture2D(256, 256, R8G8B8A8_UNORM)
random_buffer = Buffer(target.size, HEAP_UPLOAD)

window = glfw.create_window(
    target.width, target.height, 'Random', None, None)

if platform.system() == 'Windows':
    swapchain = Swapchain(glfw.get_win32_window(
        window), B8G8R8A8_UNORM, 3)
elif platform.system() == 'Darwin':
    from compushady.backends.metal import create_metal_layer
    ca_metal_layer = create_metal_layer(glfw.get_cocoa_window(window), B8G8R8A8_UNORM)
    swapchain = Swapchain(ca_metal_layer, B8G8R8A8_UNORM, 2)
else:
    swapchain = Swapchain((glfw.get_x11_display(), glfw.get_x11_window(
        window)), B8G8R8A8_UNORM, 3)

while not glfw.window_should_close(window):
    glfw.poll_events()
    random_buffer.upload(bytes([random.randint(0, 255), random.randint(
        0, 255), random.randint(0, 255), 255]) * (target.size // 4))
    random_buffer.copy_to(target)
    swapchain.present(target)

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

import glfw
import compushady.config
import compushady.formats
import compushady
from compushady.shaders import hlsl
import platform

compushady.config.set_debug(True)

print('Using device', compushady.get_current_device().name,
      'with backend', compushady.get_backend().name)

target = compushady.Texture2D(512, 512, compushady.formats.R8G8B8A8_UNORM)

shader = hlsl.compile("""

RWTexture2D<float4> target : register(u0);

[numthreads(8,8,1)]
void main(int3 tid : SV_DispatchThreadID)
{
    float4 color;
    color.r = 1;
    target[tid.xy] = color;
}
""")

compute = compushady.Compute(shader, uav=[target])

glfw.init()
# we do not want implicit OpenGL!
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

window = glfw.create_window(
    target.width, target.height, 'Mandlebrot', None, None)

if platform.system() == 'Windows':
    swapchain = compushady.Swapchain(glfw.get_win32_window(
        window), compushady.formats.B8G8R8A8_UNORM, 3)
elif platform.system() == 'Darwin':
    from compushady.backends.metal import create_metal_layer
    ca_metal_layer = create_metal_layer(glfw.get_cocoa_window(window), compushady.formats.B8G8R8A8_UNORM)
    swapchain = compushady.Swapchain(ca_metal_layer, compushady.formats.B8G8R8A8_UNORM, 3)
else:
    swapchain = compushady.Swapchain((glfw.get_x11_display(), glfw.get_x11_window(
        window)), compushady.formats.B8G8R8A8_UNORM, 3)

while not glfw.window_should_close(window):
    glfw.poll_events()
    compute.dispatch(target.width // 8, target.height // 8, 1)
    swapchain.present(target)

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

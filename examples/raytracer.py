import glfw
import compushady.config
import compushady.formats
import compushady
from compushady.shaders import hlsl
import struct
import platform
import math

compushady.config.set_debug(True)

print('Using device', compushady.get_current_device().name)

target = compushady.Texture2D(1024, 1024, compushady.formats.B8G8R8A8_UNORM)

shader = hlsl.compile("""

RWTexture2D<float4> target : register(u0);

float shot_ray(float2 xy)
{
    return 1;
}

[numthreads(8,8,1)]
void main(int3 tid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    target.GetDimensions(width, height);
    float2 xy = (tid.xy / float2(width, height)) * 2 - 1;
    float m = shot_ray(xy);
    target[tid.xy] = float4(m, 0, 0, 1);
}
""")

compute = compushady.Compute(shader, uav=[target])

glfw.init()
# we do not want implicit OpenGL!
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

window = glfw.create_window(
    target.width, target.height, "Raytracer", None, None)

if platform.system() == 'Windows':
    swapchain = compushady.Swapchain(glfw.get_win32_window(
        window), compushady.formats.B8G8R8A8_UNORM, 3)
else:
    swapchain = compushady.Swapchain((glfw.get_x11_display(), glfw.get_x11_window(
        window)), compushady.formats.B8G8R8A8_UNORM, 5)

multiplier = 0

while not glfw.window_should_close(window):
    glfw.poll_events()
    compute.dispatch(target.width // 8, target.height // 8, 1)
    swapchain.present(target)
    multiplier += 0.02

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

import glfw
import compushady.config
import compushady.formats
import compushady
from compushady.shaders import hlsl
import struct
import platform
import math

compushady.config.set_backend('vulkan')
compushady.config.set_debug(True)

print('Using device', compushady.get_best_device().name)

target = compushady.Texture2D(2048, 2048, compushady.formats.B8G8R8A8_UNORM)

config = compushady.Buffer(4, compushady.HEAP_UPLOAD)

shader = hlsl.compile("""
struct Config
{
    float multiplier;
};

RWTexture2D<float4> target : register(u0);

ConstantBuffer<Config> config : register(b0);

float mandlebrot(float2 xy)
{
    const uint max_iterations = 100;
    xy = (xy - 0.5) * 2 - float2(1, 0);
    float2 z = float2(0, 0);
    for(uint i = 0; i < max_iterations; i++)
    {
        z = float2(z.x * z.x - z.y * z.y, z.x * z.y * 2) + xy;
        if (length(z) > config.multiplier * 2) return float(i) / max_iterations;
    }

    return 1; // white
 }

[numthreads(8,8,1)]
void main(int3 tid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    target.GetDimensions(width, height);
    float2 xy = tid.xy / float2(width, height);
    float m = mandlebrot(xy);
    target[tid.xy] = float4(m, 0, config.multiplier, 1);
}
""")

compute = compushady.Compute(shader, cbv=[config], uav=[target])

glfw.init()
# we do not want implicit OpenGL!
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

window = glfw.create_window(target.width, target.height, "Mandlebrot", None, None)

if platform.system() == 'Windows':
    swapchain = compushady.Swapchain(glfw.get_win32_window(
        window), compushady.formats.B8G8R8A8_UNORM, 3)
else:
    swapchain = compushady.Swapchain((glfw.get_x11_display(), glfw.get_x11_window(
        window)), compushady.formats.B8G8R8A8_UNORM, 5)

multiplier = 0

while not glfw.window_should_close(window):
    glfw.poll_events()
    config.upload(struct.pack('f', abs(math.sin(multiplier))))
    compute.dispatch(target.width // 8, target.height // 8, 1)
    swapchain.present(target)
    multiplier += 0.02

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

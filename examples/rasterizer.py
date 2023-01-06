import glfw
import compushady.config
import compushady.formats
import compushady
from compushady.shaders import hlsl
import struct
import platform
import os

compushady.config.set_debug(True)

print('Using device', compushady.get_current_device().name)

buffer = compushady.Buffer(512 * 512 * 4, compushady.HEAP_UPLOAD)
buffer.upload(b'\xFF\x00\x00\x00' * 512 * 512)

texture = compushady.Texture2D(512, 512, compushady.formats.R8G8B8A8_UNORM)
buffer.copy_to(texture)

target = compushady.Texture2D(512, 512, compushady.formats.B8G8R8A8_UNORM)

staging_buffer = compushady.Buffer(4 * 2 * 3 * 2, compushady.HEAP_UPLOAD)
staging_buffer.upload(struct.pack('IIIIIIIIIIII', 10, 10,
                      200, 5, 50, 100, 10, 110, 200, 105, 50, 200))

vertices = compushady.Buffer(
    4 * 2 * 3 * 2, format=compushady.formats.R32G32_UINT)
staging_buffer.copy_to(vertices)

shader = hlsl.compile("""
Buffer<uint2> vertices : register(t0);
RWTexture2D<float4> target : register(u0);

float3 barycentric(float2 a, float2 b, float2 c, float2 p)
{
    float3 x = float3(c.x - a.x, b.x - a.x, a.x - p.x);
    float3 y = float3(c.y - a.y, b.y - a.y, a.y - p.y);
    float3 u = cross(x, y);

    if (abs(u.z) < 1.0)
    {
        return float3(-1, 1, 1);
    }

    return float3(1.0 - (u.x+u.y)/u.z, u.y/u.z, u.x/u.z);
}

void draw_triangle(uint2 a, uint2 b, uint2 c, uint2 p)
{
    float3 bc = barycentric(a, b, c, p);
    if (bc.x < 0 || bc.y < 0 || bc.z < 0)
    {
        return;
    }
    target[p] = float4(bc.x, bc.y, bc.z, 1);
}

[numthreads(8,8,1)]
void main(int3 tid : SV_DispatchThreadID)
{
    uint2 a = vertices[tid.z * 3];
    uint2 b = vertices[tid.z * 3 + 1];
    uint2 c = vertices[tid.z * 3 + 2];
   
    draw_triangle(a, b, c, uint2(tid.x, tid.y));
}
""")

compute = compushady.Compute(shader, srv=[vertices], uav=[target])

glfw.init()
# we do not want implicit OpenGL!
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

window = glfw.create_window(target.width, target.height, "Rasterizer", None, None)

if platform.system() == 'Windows':
    swapchain = compushady.Swapchain(glfw.get_win32_window(
        window), compushady.formats.B8G8R8A8_UNORM, 3)
elif platform.system() == 'Darwin':
    from compushady.backends.metal import create_metal_layer
    ca_metal_layer = create_metal_layer(glfw.get_cocoa_window(window), compushady.formats.B8G8R8A8_UNORM)
    swapchain = compushady.Swapchain(ca_metal_layer, compushady.formats.B8G8R8A8_UNORM, 3)
else:
    if os.environ.get('XDG_SESSION_TYPE') == 'wayland':
        swapchain = compushady.Swapchain((glfw.get_wayland_display(), glfw.get_wayland_window(
            window)), compushady.formats.B8G8R8A8_UNORM, 3, None, target.width, target.height)
    else:
        swapchain = compushady.Swapchain((glfw.get_x11_display(), glfw.get_x11_window(
            window)), compushady.formats.B8G8R8A8_UNORM, 3)

x = 0
y = 0
while not glfw.window_should_close(window):
    glfw.poll_events()
    compute.dispatch(target.width // 8, target.height // 8, 2)
    swapchain.present(target, x, y)
    x += 1
    y += 1

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

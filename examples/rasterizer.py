import glfw
import compushady.config
import compushady.formats
import compushady
from compushady.shaders import hlsl
import struct

compushady.config.set_backend('vulkan')
compushady.config.set_debug(True)

buffer = compushady.Buffer(512 * 512 * 4, compushady.HEAP_UPLOAD)
buffer.upload(b'\xFF\x00\x00\x00' * 512 * 512)

print(buffer)

texture = compushady.Texture2D(512, 512, compushady.formats.R8G8B8A8_UNORM)
print(texture)
buffer.copy_to(texture)

print(texture)

target = compushady.Texture2D(512, 512, compushady.formats.R8G8B8A8_UNORM)

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

void draw_line(int2 a, float3 a_color, int2 b, float3 b_color)
{
    uint line_length = distance(a, b);
    for(uint i = 0; i < line_length; i++)
    {
        float gradient = float(i) / float(line_length);
        float x = float(a.x) + (float(b.x - a.x) * gradient);
        float y = float(a.y) + (float(b.y - a.y) * gradient);
        uint2 xy = uint2(uint(x), uint(y));
        target[xy] = float4(a_color, 1);
    }
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
    
    /*draw_line(a, float3(1, 0, 0), b, float3(0, 1, 0));
    draw_line(b, float3(0, 1, 0), c, float3(0, 0, 1));
    draw_line(c, float3(0, 0, 1), a, float3(1, 0, 0));*/
}
""")

compute = compushady.Compute(shader, srv=[vertices], uav=[target])


def main():
    # Initialize the library
    if not glfw.init():
        return
    glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)
    # Create a windowed mode window and its OpenGL context
    window = glfw.create_window(1024, 1024, "Hello World", None, None)
    if not window:
        glfw.terminate()
        return

    swapchain = compushady.Swapchain(glfw.get_win32_window(
        window), compushady.formats.R8G8B8A8_UNORM_SRGB)

    x = 0
    y = 0
    while not glfw.window_should_close(window):
        compute.dispatch(target.width // 8, target.height // 8, 2)
        swapchain.present(target, x, y)
        x += 1
        y += 1
        glfw.poll_events()

    glfw.terminate()


if __name__ == "__main__":
    main()

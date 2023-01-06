import time
from utils import perspective_matrix_fov, scale_matrix, translation_matrix, rotation_matrix_y, identity_matrix, GLTF
import glfw
import compushady.config
import compushady.formats
import compushady
from compushady.shaders import hlsl
import platform
from utils import vector3
import numpy
import threading
import struct
import os

compushady.config.set_debug(True)

print('Using device', compushady.get_current_device().name,
      'with backend', compushady.get_backend().name)

target = compushady.Texture2D(512, 512, compushady.formats.B8G8R8A8_UNORM)
depth = compushady.Texture2D(
    target.width, target.height, compushady.formats.R32_FLOAT)

gltf = GLTF('Duck.gltf')
indices = gltf.get_indices(0)
vertices = gltf.get_vertices(0)
normals = gltf.get_normals(0)
ntriangles = gltf.get_nvertices(0) // 3

staging_buffer = compushady.Buffer(len(indices), compushady.HEAP_UPLOAD)
staging_buffer.upload(indices)

index_buffer = compushady.Buffer(
    len(indices), format=compushady.formats.R16_UINT)
staging_buffer.copy_to(index_buffer)

staging_buffer = compushady.Buffer(
    len(vertices) + len(vertices) // 3, compushady.HEAP_UPLOAD)
staging_buffer.upload_chunked(vertices, 12, struct.pack('f', 1))

vertex_buffer = compushady.Buffer(
    staging_buffer.size, format=compushady.formats.R32G32B32A32_FLOAT)
staging_buffer.copy_to(vertex_buffer)

staging_buffer = compushady.Buffer(
    len(normals) + len(normals) // 3, compushady.HEAP_UPLOAD)
staging_buffer.upload_chunked(normals, 12, struct.pack('f', 0))

normal_buffer = compushady.Buffer(
    staging_buffer.size, format=compushady.formats.R32G32B32A32_FLOAT)
staging_buffer.copy_to(normal_buffer)

model_view = identity_matrix()
perspective = identity_matrix()
config = compushady.Buffer(
    model_view.nbytes + perspective.nbytes, compushady.HEAP_UPLOAD)

clear_screen = compushady.Compute(hlsl.compile("""
RWTexture2D<float4> target : register(u0);
RWTexture2D<float> depth : register(u1);
[numthreads(8,8,1)]
void main(int3 tid : SV_DispatchThreadID)
{
    target[tid.xy] = float4(0, 0, 0, 1);
    depth[tid.xy] = 0;
}
"""), uav=[target, depth])

shader = hlsl.compile("""
struct Config
{
    matrix model_view;
    matrix perspective;
};
ConstantBuffer<Config> config : register(b0);
Buffer<uint> indices : register(t0);
Buffer<float4> vertices : register(t1);
Buffer<float4> normals : register(t2);
RWTexture2D<float4> target : register(u0);
RWTexture2D<float> depth : register(u1);

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

uint2 to_pixel(float4 v, uint width, uint height)
{   
    return uint2((v.x + 1) * 0.5 * width, (1 - v.y) * 0.5 * height);
}

void draw_triangle(uint index_a, uint index_b, uint index_c, uint2 p)
{
    uint width;
    uint height;
    target.GetDimensions(width, height);

    matrix mvp = mul(config.model_view, config.perspective);

    float4 va = mul(vertices[index_a], mvp);
    float4 vb = mul(vertices[index_b], mvp);
    float4 vc = mul(vertices[index_c], mvp);

    uint2 a = to_pixel(va / va.w, width, height);
    uint2 b = to_pixel(vb / vb.w, width, height);
    uint2 c = to_pixel(vc / vc.w, width, height);

    float3 bc = barycentric(a, b, c, p);
    if (bc.x < 0 || bc.y < 0 || bc.z < 0)
    {
        return;
    }

    float z = va.z * bc.x + vb.z * bc.y + vb.z * (1 - bc.x - bc.y);

    if (z > depth[p])
    {
        depth[p] = z;
    }
    else
    {
       return;
    }

    float3 na = mul(normals[index_a], config.model_view).xyz;
    float3 nb = mul(normals[index_b], config.model_view).xyz;
    float3 nc = mul(normals[index_c], config.model_view).xyz;

    float3 normal = na * bc.x + nb * bc.y + nc * (1 - bc.x - bc.y);

    float lambert = clamp(dot(normalize(normal), float3(0, 0, -1)), 0, 1);

    target[p] = float4(float3(1, 1, 0) * lambert, 1);
}

[numthreads(8, 8, 1)]
void main(int3 tid : SV_DispatchThreadID)
{
    uint nvertices;
    indices.GetDimensions(nvertices);

    for(uint i = 0; i < nvertices ; i += 3)
    {
        draw_triangle(indices[i], indices[i+1], indices[i+2], tid.xy);
    }
}
""")

compute = compushady.Compute(shader, cbv=[config], srv=[index_buffer,
                             vertex_buffer, normal_buffer], uav=[target, depth])

glfw.init()
# we do not want implicit OpenGL!
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

window = glfw.create_window(
    target.width, target.height, 'Rasterizer', None, None)

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


running = True


def render_loop():
    global running
    z = -1
    y = 0
    while running:
        matrix_p = perspective_matrix_fov(numpy.radians(
            30), target.width / target.height, 0.01, 1000)
        matrix_mv = translation_matrix(0, -1, z) @ rotation_matrix_y(numpy.radians(y)) @ scale_matrix(
            0.01, 0.01, 0.01)
        config.upload(matrix_mv.tobytes() + matrix_p.tobytes())
        clear_screen.dispatch(target.width // 8, target.height // 8, 1)
        compute.dispatch(target.width // 8, target.height // 8, 1)
        swapchain.present(target, 0, 0)
        z -= 0.01
        y += 1


t = threading.Thread(target=render_loop)
t.start()

while not glfw.window_should_close(window):
    glfw.poll_events()

running = False

t.join()  # wait for render thread

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

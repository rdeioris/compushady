from compushady import (
    Compute,
    Rasterizer,
    SHADER_TARGET_TYPE_VS,
    SHADER_TARGET_TYPE_PS,
    Buffer,
    Texture2D,
    HEAP_DEFAULT,
    HEAP_UPLOAD,
    Sampler,
)
from compushady.shaders import hlsl
from compushady.formats import (
    B8G8R8A8_UNORM,
    D24_UNORM_S8_UINT,
    R32_UINT,
    R16_UINT,
    R32G32B32_FLOAT,
    R8G8B8A8_UNORM,
    R32G32_FLOAT,
    R16G16B16A16_UINT,
    R32G32B32A32_FLOAT,
    D32_FLOAT
)
import compushady.config

import glfw
import platform
import struct
import sys
from pyrr import Matrix44, Quaternion
import numpy


compushady.config.set_debug(True)

print(
    "Using device",
    compushady.get_current_device().name,
    "with backend",
    compushady.get_backend().name,
)


def upload_to_gpu(data, stride, format):
    buffer = Buffer(
        size=len(data), stride=stride, format=format, heap_type=HEAP_DEFAULT
    )
    staging = Buffer(size=len(data), heap_type=HEAP_UPLOAD)
    staging.upload(data)
    staging.copy_to(buffer)
    return buffer


vertex_buffer = upload_to_gpu(
    struct.pack('9f', 0, 1, 0, -1, -1, 0, 1, -1, 0),
    stride=12,  # 4 * 3,
    format=0,
)
vertex_count = 3

vs = hlsl.compile(
    """
struct Vertex
{
    float3 position;
};

struct Output
{
    float4 position : SV_Position;
};

StructuredBuffer<Vertex> vertices : register(t0);

Output main(uint vid : SV_VertexID)
{
    Output output;
    
    float4 vertex = float4(vertices[vid].position.xyz, 1);

    output.position = vertex;
    return output;
}
""",
    target_type=SHADER_TARGET_TYPE_VS,
)

ps = hlsl.compile(
    """

struct Output
{
    float4 position : SV_Position;
};

float4 main(Output output) : SV_Target
{
    return float4(1, 0, 0, 1);
}
""",
    target_type=SHADER_TARGET_TYPE_PS,
)

print('VS', vs)
print('PS', ps)


glfw.init()
# we do not want implicit OpenGL!
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

target = Texture2D(768, 768, B8G8R8A8_UNORM)
depth = Texture2D(768, 768, D32_FLOAT)

rasterizer = Rasterizer(
    vs,
    ps,
    rtv=[target],
    dsv=depth,
    srv=[
        vertex_buffer,

    ],
    wireframe=True,
)

print("done", rasterizer)

window = glfw.create_window(
    target.width, target.height, "Rasterizer", None, None)

if platform.system() == "Windows":
    swapchain = compushady.Swapchain(
        glfw.get_win32_window(window), compushady.formats.B8G8R8A8_UNORM, 3
    )
elif platform.system() == "Darwin":
    from compushady.backends.metal import create_metal_layer

    ca_metal_layer = create_metal_layer(
        glfw.get_cocoa_window(window), compushady.formats.B8G8R8A8_UNORM
    )
    swapchain = compushady.Swapchain(
        ca_metal_layer, compushady.formats.B8G8R8A8_UNORM, 3
    )
else:
    swapchain = compushady.Swapchain(
        (glfw.get_x11_display(), glfw.get_x11_window(window)),
        compushady.formats.B8G8R8A8_UNORM,
        3,
    )


while not glfw.window_should_close(window):
    glfw.poll_events()
    
    rasterizer.draw(vertex_count)
    swapchain.present(target)

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

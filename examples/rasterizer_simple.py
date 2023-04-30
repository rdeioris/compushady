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
)
import compushady.config

import glfw
import platform
import struct
import sys
from pyrr import Matrix44
import numpy

from dugltf import DuGLTF, GLTF_USHORT, GLTF_INT, GLTF_FLOAT
from PIL import Image
import io

compushady.config.set_debug(True)

print(
    "Using device",
    compushady.get_current_device().name,
    "with backend",
    compushady.get_backend().name,
)

gltf = DuGLTF(sys.argv[1])

found_node = None

for node in gltf.get_nodes():
    if "mesh" in node:  # and "skin" in node:
        found_node = node
        break

if not found_node:
    raise Exception("skinned mesh not found")


def upload_to_gpu(data, stride, format):
    buffer = Buffer(
        size=len(data), stride=stride, format=format, heap_type=HEAP_DEFAULT
    )
    staging = Buffer(size=len(data), heap_type=HEAP_UPLOAD)
    staging.upload(data)
    staging.copy_to(buffer)
    return buffer


for primitive in gltf.get_primitives(found_node["mesh"]):
    print(primitive)
    if "indices" in primitive:
        component_type = gltf.get_accessor_component_type(primitive["indices"])
        data = gltf.get_accessor_data(primitive["indices"])
        index_buffer = upload_to_gpu(
            data,
            stride=0,  # 2 if component_type == GLTF_USHORT else 4,
            format=R16_UINT if component_type == GLTF_USHORT else R32_UINT,
        )
        index_count = gltf.get_accessor_count(primitive["indices"])
        output_buffer = Buffer(
            size=len(data),
            format=R16_UINT if component_type == GLTF_USHORT else R32_UINT,
            heap_type=HEAP_DEFAULT,
        )
    for attribute in primitive["attributes"]:
        if attribute == "POSITION":
            data = gltf.get_accessor_data(primitive["attributes"]["POSITION"])
            vertex_buffer = upload_to_gpu(
                data,
                stride=0,  # 4 * 3,
                format=R32G32B32_FLOAT,
            )
            vertex_count = gltf.get_accessor_count(primitive["attributes"]["POSITION"])
        elif attribute == "TEXCOORD_0":
            data = gltf.get_accessor_data(primitive["attributes"]["TEXCOORD_0"])
            uv_buffer = upload_to_gpu(
                data,
                stride=0,  # 4 * 3,
                format=R32G32_FLOAT,
            )
    if "material" in primitive:
        material = gltf.get_material(primitive["material"])
        print(material)
        data, mime_type = gltf.get_image_data(0)
        image = Image.open(io.BytesIO(data)).convert("RGBA")
        print(image.size)
        texture = Texture2D(image.size[0], image.size[1], R8G8B8A8_UNORM)
        staging = Buffer(texture.size, HEAP_UPLOAD)
        staging.upload2d(
            image.tobytes(), texture.row_pitch, texture.width, texture.height, 4
        )
        staging.copy_to(texture)


print(index_buffer, vertex_buffer, index_buffer.size, index_count, vertex_count)

transform = Buffer(4 * 16 * 2, HEAP_UPLOAD)
world = Matrix44.from_translation(
    (0, 0, -2), dtype=numpy.float32
) * Matrix44.from_x_rotation(numpy.radians(-90), dtype=numpy.float32)
perspective = Matrix44.perspective_projection(
    90.0, 1.0, 0.01, 1000.0, dtype=numpy.float32
)

print(perspective)
transform.upload(world.tobytes() + perspective.tobytes())

vs = hlsl.compile(
    """
struct Vertex
{
    float3 position;
};

struct Output
{
    float4 position : SV_Position;
    float2 uv: UV;
    float3 color : COLOR;
};

struct Transform
{
    float4x4 world;
    float4x4 projection;
};

ConstantBuffer<Transform> transform: register(b0);
Buffer<uint> indices : register(t0);
Buffer<float3> vertices : register(t1);
Buffer<float2> uvs : register(t2);
RWStructuredBuffer<uint> output_buffer : register(u0);
Output main(uint vid : SV_VertexID)
{
    Output output;
    uint index = indices[vid];
    float4 world_pos = mul(transform.world, float4(vertices[vid].xyz, 1));
    output.position = mul(transform.projection, world_pos);
    output.color = float3(1, 1, 0);
    output.uv = uvs[vid];
    output_buffer[vid] = index;
    return output;
}
""",
    target_type=SHADER_TARGET_TYPE_VS,
)

ps = hlsl.compile(
    """

SamplerState sampler0 : register(s0);
Texture2D<float4> texture : register(t3);

struct Output
{
    float4 position : SV_Position;
    float2 uv: UV;
    float3 color : COLOR;
};

float4 main(Output output) : SV_Target
{
    return float4(texture.Sample(sampler0, output.uv).rgb, 1);
}
""",
    target_type=SHADER_TARGET_TYPE_PS,
)


glfw.init()
# we do not want implicit OpenGL!
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

target = Texture2D(1024, 1024, B8G8R8A8_UNORM)
depth = Texture2D(1024, 1024, D24_UNORM_S8_UINT)

rasterizer = Rasterizer(
    vs,
    ps,
    rtv=[target],
    dsv=depth,
    cbv=[transform],
    srv=[index_buffer, vertex_buffer, uv_buffer, texture],
    uav=[output_buffer],
    samplers=[Sampler()],
    wireframe=False,
)

print("done", rasterizer)

window = glfw.create_window(target.width, target.height, "Rasterizer", None, None)

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

clear = Compute(
    hlsl.compile(
        """
RWTexture2D<float4> target : register(u0);

[numthreads(8, 8, 1)]
void main(int3 tid : SV_DispatchThreadID)
{
    target[tid.xy] = float4(0, 0, 0, 0);
}
"""
    ),
    uav=[target],
)

rot = 0
while not glfw.window_should_close(window):
    glfw.poll_events()
    clear.dispatch(1024 // 8, 1024 // 8, 1)
    rot += 0.01
    world = Matrix44.from_translation(
        (0, numpy.sin(rot), -5), dtype=numpy.float32
    ) * Matrix44.from_y_rotation(rot, dtype=numpy.float32)
    transform.upload(world.tobytes() + perspective.tobytes())
    # rasterizer.draw(index_count)
    rasterizer.draw_indexed(index_buffer, index_count)
    swapchain.present(target)

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

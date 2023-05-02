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
        print(attribute)
        if attribute == "POSITION":
            data = gltf.get_accessor_data(primitive["attributes"]["POSITION"])
            vertex_buffer = upload_to_gpu(
                data,
                stride=12,  # 4 * 3,
                format=0  # R32G32B32_FLOAT,
            )
            vertex_count = gltf.get_accessor_count(
                primitive["attributes"]["POSITION"])
        elif attribute == "TEXCOORD_0":
            data = gltf.get_accessor_data(
                primitive["attributes"]["TEXCOORD_0"])
            uv_buffer = upload_to_gpu(
                data,
                stride=0,  # 4 * 3,
                format=R32G32_FLOAT,
            )
        elif attribute == "JOINTS_0":
            data = gltf.get_accessor_data(primitive["attributes"]["JOINTS_0"])
            joints_buffer = upload_to_gpu(
                data,
                stride=0,  # 4 * 3,
                format=R16G16B16A16_UINT,
            )
        elif attribute == "WEIGHTS_0":
            data = gltf.get_accessor_data(primitive["attributes"]["WEIGHTS_0"])
            weights_buffer = upload_to_gpu(
                data,
                stride=0,  # 4 * 3,
                format=R32G32B32A32_FLOAT,
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

    inverse_bind_matrices_buffer = upload_to_gpu(
        gltf.get_inverse_bind_matrices(0), stride=64, format=0
    )

    pose_data = [None] * len(gltf.get_joints(0))
    for joint_id, node_id in enumerate(gltf.get_joints(0)):
        parent_id = gltf.get_node_parent(node_id)

        try:
            parent_matrix = pose_data[gltf.get_joint_from_node(0, parent_id)]

            print(joint_id, "parent", parent_id,
                  gltf.get_joint_from_node(0, parent_id))
        except:
            print("not found for joint", joint_id)
            parent_matrix = Matrix44.identity(dtype=numpy.float32)

        sampler_id = gltf.get_animation_sampler_by_node_and_path(
            0, node_id, "rotation")
        frame = gltf.get_animation_sampler_frames(
            gltf.get_animation_sampler(0, sampler_id), "rotation"
        )[0]
        q = Quaternion(frame[1])

        sampler_id = gltf.get_animation_sampler_by_node_and_path(
            0, node_id, "translation"
        )
        frame = gltf.get_animation_sampler_frames(
            gltf.get_animation_sampler(0, sampler_id), "translation"
        )[0]

        # print(frame)

        frame_matrix = Matrix44.from_translation(
            frame[1], dtype=numpy.float32
        ) * Matrix44.from_quaternion(q, dtype=numpy.float32)

        print(gltf.get_node(node_id)["name"], frame[1], q)

        pose_data[joint_id] = parent_matrix * frame_matrix

        # print(pose_data[-1])
        continue

        bone_matrix_inverse = Matrix44(
            gltf.get_inverse_bind_matrix(0, joint_id), dtype=numpy.float32
        )
        bone_matrix = bone_matrix_inverse.inverse

        pose_data.append(bone_matrix)

    pose_bytes = b""
    for pose in pose_data:
        pose_bytes += pose.tobytes()
    pose_buffer = upload_to_gpu(pose_bytes, stride=64, format=0)

    print(len(gltf.get_joints(0)), len(pose_bytes), len(pose_data))


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

Buffer<uint4> joints : register(t4);
Buffer<float4> weights : register(t5);
StructuredBuffer<float4x4> inverse_bind_matrices: register(t6);
StructuredBuffer<float4x4> pose: register(t7);

Output main(uint vid : SV_VertexID)
{
    Output output;
    uint index = indices[vid];

    uint bone_index0 = joints[index].x;
    uint bone_index1 = joints[index].y;
    uint bone_index2 = joints[index].z;
    uint bone_index3 = joints[index].w;

    float weight0 = weights[index].x;
    float weight1 = weights[index].y;
    float weight2 = weights[index].z;
    float weight3 = weights[index].w;

    output_buffer[vid] = bone_index0;

    float3 vertex = vertices[index];

    float4x4 bone_matrix0 = mul(pose[bone_index0], inverse_bind_matrices[bone_index0]);
    float4 pos0 = mul(bone_matrix0, float4(vertex, 1));

    float4x4 bone_matrix1 = mul(pose[bone_index1], inverse_bind_matrices[bone_index1]);
    float4 pos1 = mul(bone_matrix1, float4(vertex, 1));

    float4x4 bone_matrix2 = mul(pose[bone_index2], inverse_bind_matrices[bone_index2]);
    float4 pos2 = mul(bone_matrix2, float4(vertex, 1));

    float4x4 bone_matrix3 = mul(pose[bone_index3], inverse_bind_matrices[bone_index3]);
    float4 pos3 = mul(bone_matrix3, float4(vertex, 1));

    /*float4x4 bone_matrix0 = mul(inverse_bind_matrices[bone_index0], pose[bone_index0]);
    float4 pos0 = mul(bone_matrix0, float4(vertex, 1));

    float4x4 bone_matrix1 = mul(inverse_bind_matrices[bone_index1], pose[bone_index1]);
    float4 pos1 = mul(bone_matrix1, float4(vertex, 1));

    float4x4 bone_matrix2 = mul(inverse_bind_matrices[bone_index2], pose[bone_index2]);
    float4 pos2 = mul(bone_matrix2, float4(vertex, 1));

    float4x4 bone_matrix3 = mul(inverse_bind_matrices[bone_index3], pose[bone_index3]);
    float4 pos3 = mul(bone_matrix3, float4(vertex, 1));*/
   
    float4 world_pos = mul(transform.world, float4(pos0.xyz * weight0 + pos1.xyz * weight1 + pos2.xyz * weight2 + pos3.xyz * weight3, 1));

    output.position = mul(transform.projection, world_pos);
    output.color = float3(1, 1, 0);
    output.uv = uvs[index];
  
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
    cbv=[transform],
    srv=[
        index_buffer,
        vertex_buffer,
        uv_buffer,
        texture,
        joints_buffer,
        weights_buffer,
        inverse_bind_matrices_buffer,
        pose_buffer,
    ],
    uav=[output_buffer],
    samplers=[Sampler()],
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
print('loop')
rot = numpy.radians(90)
while not glfw.window_should_close(window):
    glfw.poll_events()
    clear.dispatch(1024 // 8, 1024 // 8, 1)
    rot += 0.01
    world = (
        Matrix44.from_translation((0, -1, -2), dtype=numpy.float32)
        * Matrix44.from_z_rotation(rot, dtype=numpy.float32)
        * Matrix44.from_y_rotation(numpy.radians(90), dtype=numpy.float32)
    )

    transform.upload(world.tobytes() + perspective.tobytes())
    rasterizer.draw(index_count)
    swapchain.present(target)

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

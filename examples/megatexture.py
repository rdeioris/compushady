import glfw


from compushady import (
    Texture2D,
    Compute,
    Heap,
    HEAP_DEFAULT,
    Buffer,
    HEAP_UPLOAD,
    Sampler,
)
from compushady.utils import create_swapchain_from_glfw, load_image
from compushady.formats import B8G8R8A8_UNORM, R8G8B8A8_UNORM
from compushady.shaders import hlsl
import compushady.config

compushady.config.set_debug(True)

import math
import random
import os

glfw.init()
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

target = Texture2D(512, 512, B8G8R8A8_UNORM)

testscreen, width, height = load_image(
    os.path.join(os.path.dirname(__file__), "testscreen.png")
)

window = glfw.create_window(
    target.width, target.height, "Texture Streaming", None, None
)

swapchain = create_swapchain_from_glfw(window)

megatexture = Texture2D(4096, 4096, R8G8B8A8_UNORM, sparse=True)

heaps = []
num_heaps = (megatexture.tiles_x * megatexture.tiles_y) // 4
for i in range(0, megatexture.tiles_x * megatexture.tiles_y):
    if i < num_heaps:
        heaps.append(Heap(HEAP_DEFAULT, 64 * 1024))
    else:
        heaps.append(None)

staging_texture = Texture2D(
    megatexture.tile_width, megatexture.tile_height, R8G8B8A8_UNORM
)

staging_buffer = Buffer(staging_texture.size, HEAP_UPLOAD)


def load_chunk_to_tile(x, y):

    image_chunk = b""
    for row in range(y, y + megatexture.tile_height):
        image_chunk += testscreen[
            row * megatexture.width * 4
            + x * 4 : row * megatexture.width * 4
            + (x + megatexture.tile_width) * 4
        ]

    staging_buffer.upload(image_chunk)
    staging_buffer.copy_to(staging_texture)

    staging_texture.copy_to(
        megatexture,
        dst_x=x,
        dst_y=y,
        width=megatexture.tile_width,
        height=megatexture.tile_height,
    )


compute = Compute(
    hlsl.compile(
        """
Texture2D<float4> megatexture;
RWTexture2D<float4> target;
SamplerState sampler0;

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadId)
{
    uint status = 0;
    float2 uv = float2(tid.xy) / float2(512, 512);
   
    const float4 color = megatexture.SampleLevel(sampler0, uv, 0, int2(0, 0), status);
    if (CheckAccessFullyMapped(status))
    {
        target[tid.xy] = color;
    }
    else
    {
        target[tid.xy] = float4(1, 0, 0, 1);
    }
}
"""
    ),
    srv=[megatexture],
    uav=[target],
    samplers=[Sampler()],
)

while not glfw.window_should_close(window):
    glfw.poll_events()

    compute.dispatch(math.ceil(target.width / 8), math.ceil(target.height / 8), 1)

    random.shuffle(heaps)

    for y in range(0, megatexture.tiles_y):
        for x in range(0, megatexture.tiles_x):
            tile = y * megatexture.tiles_x + x
            megatexture.bind_tile(x, y, 0, heaps[tile])
            if heaps[tile]:
                load_chunk_to_tile(
                    x * megatexture.tile_width, y * megatexture.tile_height
                )

    swapchain.present(target)

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

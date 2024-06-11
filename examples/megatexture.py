import glfw


from compushady import Texture2D, Compute, Heap, HEAP_DEFAULT
from compushady.utils import create_swapchain_from_glfw
from compushady.formats import B8G8R8A8_UNORM
from compushady.shaders import hlsl
import compushady.config

compushady.config.set_backend("d3d12")
compushady.config.set_debug(True)

import math
import random
import time

glfw.init()
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)


target = Texture2D(1024, 1024, B8G8R8A8_UNORM)

heaps = []

for i in range(0, 64):
    heaps.append(Heap(HEAP_DEFAULT, 64 * 1024))

window = glfw.create_window(
    target.width, target.height, "Texture Streaming", None, None
)

swapchain = create_swapchain_from_glfw(window)

megatexture = Texture2D(4096, 4096, B8G8R8A8_UNORM, sparse=True)

compute = Compute(
    hlsl.compile(
        """
SamplerState sampler0;
Texture2D<float4> megatexture;
RWTexture2D<float4> target;

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadId)
{
    uint status = 0;
    const float4 color = megatexture.Load(uint3(tid.xy, 0), uint2(0, 0), status);
    if (CheckAccessFullyMapped(status))
    {
        target[tid.xy] = float4(1, 0, 0, 1);
    }
    else
    {
        target[tid.xy] = float4(0, 0, 0, 1);
    }
}
""",
        "main",
        "cs_6_0",
    ),
    srv=[megatexture],
    uav=[target],
)

while not glfw.window_should_close(window):
    glfw.poll_events()

    compute.dispatch(math.ceil(target.width / 8), math.ceil(target.height / 8), 1)

    for y in range(0, 4):
        for x in range(0, 4):
            megatexture.bind_tile(x, y, 0, random.choice([None, heaps[0]]))

    swapchain.present(target)

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

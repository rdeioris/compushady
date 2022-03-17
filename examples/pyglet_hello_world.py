import pyglet
import struct
from compushady import HEAP_UPLOAD, Swapchain, Compute, Texture2D, Buffer
from compushady.formats import B8G8R8A8_UNORM
from compushady.shaders import hlsl

window = pyglet.window.Window()

swapchain = Swapchain(window._hwnd, B8G8R8A8_UNORM, 3)
target = Texture2D(window.width, window.height, B8G8R8A8_UNORM)
clear_screen = Compute(hlsl.compile("""
RWTexture2D<float4> target : register(u0);
[numthreads(8,8,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    target[tid.xy] = float4(1, 0, 0, 1);
}
"""), uav=[target])

constant_buffer = Buffer(8, HEAP_UPLOAD)

quad = Compute(hlsl.compile("""
struct Quad
{
    uint x;
    uint y;
};
ConstantBuffer<Quad> quad : register(b0);
RWTexture2D<float4> target : register(u0);
[numthreads(8,8,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    target[tid.xy + uint2(quad.x, quad.y)] = float4(0, 1, 1, 1);
}
"""), cbv=[constant_buffer], uav=[target])

x = 0
y = 0


def update(dt):
    global x, y
    x += 1
    y += 1
    if x > window.width:
        x = 0
    if y > window.height:
        y = 0
    constant_buffer.upload(struct.pack('II', x, y))


@window.event
def on_draw():
    clear_screen.dispatch(window.width // 8, window.height // 8, 1)
    quad.dispatch(1, 1, 1)
    swapchain.present(target)


pyglet.clock.schedule_interval(update, 1/120.0)

pyglet.app.run()

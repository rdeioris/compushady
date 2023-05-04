from compushady import Swapchain, Texture2D, Compute
from compushady.formats import B8G8R8A8_UNORM
from compushady.shaders import hlsl
from compushady.backends.metal import create_metal_layer
import sdl2.ext
import ctypes

sdl2.ext.init()


window = sdl2.ext.Window("Hello World!", size=(640, 480))

wminfo = sdl2.SDL_SysWMinfo()
sdl2.SDL_GetWindowWMInfo(window.window, ctypes.byref(wminfo))

ca_metal_layer = create_metal_layer(
    wminfo.info.cocoa.window, B8G8R8A8_UNORM
)
swapchain = Swapchain(
    ca_metal_layer, B8G8R8A8_UNORM, 3
)

window.show()

running = True

target = Texture2D(640, 480, B8G8R8A8_UNORM)

shader = hlsl.compile("""

RWTexture2D<float4> target : register(u0);

[numthreads(8,8,1)]
void main(int3 tid : SV_DispatchThreadID)
{
    float4 color;
    color.r = 1;
    target[tid.xy] = color;
}
""")

compute = Compute(shader, uav=[target])

while running:
    events = sdl2.ext.get_events()
    for event in events:
        if event.type == sdl2.SDL_QUIT:
            running = False
            break
    compute.dispatch(640 // 6, 480 //8, 1)
    swapchain.present(target)
    # window.refresh()

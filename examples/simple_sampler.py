import glfw
import compushady.config
import compushady.formats
import compushady
from compushady.shaders import hlsl
import platform
from PIL import Image
import os

compushady.config.set_debug(True)

print(
    "Using device",
    compushady.get_current_device().name,
    "with backend",
    compushady.get_backend().name,
)

data = Image.open("duality256.png").convert("RGBA").tobytes()

upload = compushady.Buffer(256 * 144 * 4, compushady.HEAP_UPLOAD)
upload.upload(data)

source = compushady.Texture2D(256, 144, compushady.formats.R8G8B8A8_UNORM)
upload.copy_to(source)


target = compushady.Texture2D(1024, 144 * 2, compushady.formats.B8G8R8A8_UNORM)

shader = hlsl.compile(
    """
SamplerState sampler0 : register(s0);
Texture2D<float4> source : register(t0);
RWTexture2D<float4> target : register(u0);


[numthreads(8,8,1)]
void main(int3 tid : SV_DispatchThreadID)
{
    target[tid.xy] = source.SampleLevel(sampler0, float2(float(tid.x) / 256, float(tid.y) / 144), 0) * 4;
}
"""
)

compute = compushady.Compute(
    shader, srv=[source], uav=[target], samplers=[compushady.Sampler()]
)

compute2 = compushady.Compute(
    shader,
    srv=[source],
    uav=[target],
    samplers=[
        compushady.Sampler(
            filter_min=compushady.SAMPLER_FILTER_LINEAR,
            filter_mag=compushady.SAMPLER_FILTER_LINEAR,
        )
    ],
)

compute3 = compushady.Compute(
    shader,
    srv=[source],
    uav=[target],
    samplers=[
        compushady.Sampler(
            filter_min=compushady.SAMPLER_FILTER_LINEAR,
            filter_mag=compushady.SAMPLER_FILTER_LINEAR,
            address_mode_u=compushady.SAMPLER_ADDRESS_MODE_CLAMP,
        )
    ],
)

compute4 = compushady.Compute(
    shader,
    srv=[source],
    uav=[target],
    samplers=[
        compushady.Sampler(
            filter_min=compushady.SAMPLER_FILTER_LINEAR,
            filter_mag=compushady.SAMPLER_FILTER_LINEAR,
            address_mode_u=compushady.SAMPLER_ADDRESS_MODE_MIRROR,
        )
    ],
)

compute5 = compushady.Compute(
    shader,
    srv=[source],
    uav=[target],
    samplers=[
        compushady.Sampler(
            filter_min=compushady.SAMPLER_FILTER_LINEAR,
            filter_mag=compushady.SAMPLER_FILTER_LINEAR,
            address_mode_u=compushady.SAMPLER_ADDRESS_MODE_MIRROR,
            address_mode_v=compushady.SAMPLER_ADDRESS_MODE_CLAMP,
        )
    ],
)

compute6 = compushady.Compute(
    shader,
    srv=[source],
    uav=[target],
    samplers=[
        compushady.Sampler(
            filter_min=compushady.SAMPLER_FILTER_POINT,
            filter_mag=compushady.SAMPLER_FILTER_POINT,
            address_mode_u=compushady.SAMPLER_ADDRESS_MODE_MIRROR,
            address_mode_v=compushady.SAMPLER_ADDRESS_MODE_CLAMP,
        )
    ],
)

glfw.init()
# we do not want implicit OpenGL!
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

window = glfw.create_window(target.width, target.height, "Sampler", None, None)

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
    if os.environ.get("XDG_SESSION_TYPE") == "wayland":
        swapchain = compushady.Swapchain(
            (glfw.get_wayland_display(), glfw.get_wayland_window(window)),
            compushady.formats.B8G8R8A8_UNORM,
            3,
            None,
            target.width,
            target.height,
        )
    else:
        swapchain = compushady.Swapchain(
            (glfw.get_x11_display(), glfw.get_x11_window(window)),
            compushady.formats.B8G8R8A8_UNORM,
            3,
        )

current_compute = compute
while not glfw.window_should_close(window):
    glfw.poll_events()
    if glfw.get_key(window, glfw.KEY_1):
        current_compute = compute
    if glfw.get_key(window, glfw.KEY_2):
        current_compute = compute2
    if glfw.get_key(window, glfw.KEY_3):
        current_compute = compute3
    if glfw.get_key(window, glfw.KEY_4):
        current_compute = compute4
    if glfw.get_key(window, glfw.KEY_5):
        current_compute = compute5
    if glfw.get_key(window, glfw.KEY_6):
        current_compute = compute6
    current_compute.dispatch(target.width // 8, target.height // 8, 1)
    swapchain.present(target)

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

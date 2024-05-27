import glfw
import compushady.config
import compushady.formats
import compushady
from compushady.shaders import hlsl
import struct
import platform

compushady.config.set_debug(True)

print('Using device', compushady.get_current_device().name)

target = compushady.Texture2D(512, 512, compushady.formats.B8G8R8A8_UNORM)

# we need space for 3 quads (uint4 * 3)
paddle0 = [10, 10, 10, 100]
paddle1 = [target.width - 10 - 10, 10, 10, 100]
ball = [target.width // 2, target.height // 2, 20, 20]

speed = 2 # the overall game speed

ball_direction = [1, 1] # initial ball direction

quads_staging_buffer = compushady.Buffer(4 * 4 * 3, compushady.HEAP_UPLOAD)
quads_buffer = compushady.Buffer(
    quads_staging_buffer.size, format=compushady.formats.R32G32B32A32_SINT)

# our rendering system ;)
shader = hlsl.compile("""
Buffer<int4> quads : register(t0);
RWTexture2D<float4> target : register(u0);

[numthreads(8, 8, 3)]
void main(int3 tid : SV_DispatchThreadID)
{
    int4 quad = quads[tid.z];
   
    if (tid.x > quad.x + quad.z)
        return;
    if (tid.x < quad.x)
        return;
    if (tid.y < quad.y)
        return;
    if (tid.y > quad.y + quad.w)
        return;

    target[tid.xy] = float4(1, 1, 1, 1);
}
""")

compute = compushady.Compute(shader, srv=[quads_buffer], uav=[target])

# a super simple clear screen procedure
clear_screen = compushady.Compute(hlsl.compile("""
RWTexture2D<float4> target : register(u0);

[numthreads(8, 8, 1)]
void main(int3 tid : SV_DispatchThreadID)
{
    target[tid.xy] = float4(0, 0, 0, 0);
}
"""), uav=[target])

glfw.init()
# we do not want implicit OpenGL!
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

window = glfw.create_window(target.width, target.height, 'Pong', None, None)

if platform.system() == 'Windows':
    swapchain = compushady.Swapchain(glfw.get_win32_window(
        window), compushady.formats.B8G8R8A8_UNORM, 2)
elif platform.system() == 'Darwin':
    from compushady.backends.metal import create_metal_layer
    ca_metal_layer = create_metal_layer(glfw.get_cocoa_window(window), compushady.formats.B8G8R8A8_UNORM)
    swapchain = compushady.Swapchain(
        ca_metal_layer, compushady.formats.B8G8R8A8_UNORM, 2)
else:
    swapchain = compushady.Swapchain((glfw.get_x11_display(), glfw.get_x11_window(
        window)), compushady.formats.B8G8R8A8_UNORM, 2)


def collide(source, dest):
    if source[0] + source[2] < dest[0]:
        return False
    if source[0] > dest[0] + dest[2]:
        return False
    if source[1] + source[3] < dest[1]:
        return False
    if source[1] > dest[1] + dest[3]:
        return False
    return True


while not glfw.window_should_close(window):
    glfw.poll_events()
    paddle0_effect = None
    paddle1_effect = None
    if glfw.get_key(window, glfw.KEY_W):
        paddle0[1] -= 1 * speed
        paddle0_effect = -1
    if glfw.get_key(window, glfw.KEY_S):
        paddle0[1] += 1 * speed
        paddle0_effect = 1
    if glfw.get_key(window, glfw.KEY_UP):
        paddle1[1] -= 1 * speed
        paddle1_effect = -1
    if glfw.get_key(window, glfw.KEY_DOWN):
        paddle1[1] += 1 * speed
        paddle1_effect = 1

    clear_screen.dispatch(target.width // 8, target.height // 8, 1)
    
    if collide(ball, paddle0):
        ball_direction[0] = 1
        if paddle0_effect:
            ball_direction[1] = paddle0_effect
    elif collide(ball, paddle1):
        ball_direction[0] = -1
        if paddle1_effect:
            ball_direction[1] = paddle1_effect
    else:
        if ball[0] + ball[2] >= 512:
            ball_direction[0] = -1
        if ball[0] < 0:
            ball_direction[0] = 1
        if ball[1] + ball[3] >= 512:
            ball_direction[1] = -1
        if ball[1] < 0:
            ball_direction[1] = 1

    ball[0] += ball_direction[0] * speed
    ball[1] += ball_direction[1] * speed

    quads_staging_buffer.upload(struct.pack('12i', *paddle0, *paddle1, *ball))
    quads_staging_buffer.copy_to(quads_buffer)
    compute.dispatch(target.width // 8, target.height // 8, 1)
    swapchain.present(target)

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

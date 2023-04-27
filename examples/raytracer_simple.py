import glfw
import compushady.config
import compushady
from compushady import (
    HEAP_UPLOAD,
    HEAP_READBACK,
    Buffer,
    BLAS,
    TLAS,
    SHADER_TARGET_TYPE_LIB,
    RayTracer,
    Texture2D,
)
from compushady.formats import R32G32B32_FLOAT, R8G8B8A8_UNORM, B8G8R8A8_UNORM
from compushady.shaders import hlsl
import platform
import struct
import glfw

compushady.config.set_debug(True)

print(
    "Using device",
    compushady.get_current_device().name,
    "with backend",
    compushady.get_backend().name,
)

triangle = Buffer(4 * 3 * 3, HEAP_UPLOAD, format=R32G32B32_FLOAT, stride=4 * 3)
triangle.upload(struct.pack("fffffffff", 0, 1, 0, -1, -1, 0, 1, -1, 0))

blas = BLAS(triangle)
print(blas)

tlas = TLAS(blas)
print(tlas)

shader = hlsl.compile(
    """
RaytracingAccelerationStructure scene : register(t0);
RWTexture2D<float4> output : register(u0);

struct RayPayload
{
    float4 color;
};

[shader("raygeneration")]
void main()
{
    RayDesc ray;
    float2 position = DispatchRaysIndex().xy * float2(1.0 / 512, 1.0 / 512) + float2(-1, -1);
    position *= float2(1, -1);
    ray.Origin = float3(position, -1);
    ray.Direction = float3(0, 0, 1);

    //ray.Origin = float3(0, 0, -1000);
    //float3 destination = float3(position.x, position.y, 0) + float3(0, 0, 1);
    //ray.Direction = normalize(destination - ray.Origin);

    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload;
    payload.color = float4(1, 0, 0, 1);
    
    TraceRay(scene, RAY_FLAG_NONE , ~0, 0, 1, 0, ray, payload);

    /*
    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(
        scene,
        0, // OR'd with flags above
        ~0,
        ray);
    q.Proceed();
    if(q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        output[DispatchRaysIndex().xy] = float4(1, 0, 0, 0);
    }
    else
    {
        output[DispatchRaysIndex().xy] = float4(1, 1, 1, 1);
    }
    */
    output[DispatchRaysIndex().xy] = payload.color;
}

[shader("closesthit")]
void main2(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.color = float4(attributes.barycentrics.x, 0, attributes.barycentrics.y, 1);
}

[shader("miss")]
void main3(inout RayPayload payload)
{
    payload.color = float4(0, 1, 0, 1);
}
""",
    target_type=SHADER_TARGET_TYPE_LIB,
)

target = Texture2D(1024, 1024, B8G8R8A8_UNORM)

raytracer = RayTracer(shader, srv=[tlas], uav=[target])

glfw.init()
# we do not want implicit OpenGL!
glfw.window_hint(glfw.CLIENT_API, glfw.NO_API)

window = glfw.create_window(target.width, target.height, "RayTracer", None, None)

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
    raytracer.dispatch_rays(target.width, target.height, 1)
    swapchain.present(target)

swapchain = None  # this ensures the swapchain is destroyed before the window

glfw.terminate()

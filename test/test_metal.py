import struct
import unittest
from compushady import Buffer, Compute, HEAP_UPLOAD, HEAP_READBACK, Texture2D
from compushady.shaders import msl
from compushady.formats import (
    R32G32_FLOAT,
    R32G32B32A32_FLOAT,
    R16G16B16A16_FLOAT,
    R32_UINT,
)
import compushady.config

compushady.config.set_debug(True)


@unittest.skipIf(
    compushady.get_backend().name != "metal", "Tests meaningful only with Metal backend"
)
class MetalTests(unittest.TestCase):

    def test_simple_fill32(self):
        u0 = Texture2D(2, 2, R32G32B32A32_FLOAT)
        b0 = Buffer(u0.size, HEAP_READBACK)
        shader = msl.compile(
            """
#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

kernel void main0(texture2d<float, access::write> input [[texture(0)]])
{
    input.write(float4(1.0, 2.0, 3.0, 4.0), uint2(0, 0));
}
        """,
            (1, 1, 1),
            "main0",
        )
        compute = Compute(shader, uav=[u0])
        compute.dispatch(1, 1, 1)
        u0.copy_to(b0)
        self.assertEqual(struct.unpack("4f", b0.readback(16)), (1, 2, 3, 4))

    def test_bindless_on_metal2(self):
        u0 = Texture2D(1, 1, format=R32_UINT)
        b0 = Buffer(u0.size, HEAP_READBACK)
        shader = msl.compile(
            """
#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct Arguments
{
        texture2d<uint, access::write> buffer [[id(0)]];
};

kernel void main0(device Arguments & input [[buffer(0)]])
{
    input.buffer.write(100, uint2(0, 0));
} 
        """,
            (1, 1, 1),
            "main0",
        )
        compute = Compute(shader, bindless=True)
        compute.bind_uav(0, u0)
        compute.dispatch(1, 1, 1)
        u0.copy_to(b0)
        self.assertEqual(struct.unpack("I", b0.readback(4)), (100, ))

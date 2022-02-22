import numpy
import struct
import unittest
from compushady import Buffer, Compute, HEAP_UPLOAD, HEAP_READBACK
from compushady.shaders import hlsl
from compushady.formats import R32G32B32A32_UINT, R16G16B16A16_FLOAT
import compushady.config
compushady.config.set_debug(True)


class BufferTests(unittest.TestCase):

    def test_simple_fill32(self):
        b0 = Buffer(32, format=R32G32B32A32_UINT)
        b1 = Buffer(32, HEAP_READBACK)
        shader = hlsl.compile("""
        RWBuffer<uint4> buffer : register(u0);
        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            buffer[tid.x] = uint4(tid.x + 4, 1, 2, 3);
        }
        """)
        compute = Compute(shader, uav=[b0])
        compute.dispatch(2, 1, 1)
        b0.copy_to(b1)
        self.assertEqual(struct.unpack('8I', b1.readback()),
                         (4, 1, 2, 3, 5, 1, 2, 3))

    def test_simple_fill_float16(self):
        b0 = Buffer(32, format=R16G16B16A16_FLOAT)
        b1 = Buffer(32, HEAP_READBACK)
        shader = hlsl.compile("""
        RWBuffer<float4> buffer : register(u0);
        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            buffer[tid.x] = float4(tid.x, 3, 2, 1);
        }
        """)
        compute = Compute(shader, uav=[b0])
        compute.dispatch(4, 1, 1)
        b0.copy_to(b1)
        self.assertTrue(numpy.array_equal(numpy.frombuffer(b1.readback(), dtype=numpy.float16),
                                          (0, 3, 2, 1, 1, 3, 2, 1, 2, 3, 2, 1, 3, 3, 2, 1)))

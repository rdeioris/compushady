import unittest
from compushady import Buffer, Compute, HEAP_UPLOAD, HEAP_READBACK
from compushady.shaders import hlsl
from compushady.formats import R16G16B16A16_UINT


class BufferTests(unittest.TestCase):

    def test_simple_fill(self):
        b0 = Buffer(8, format=R16G16B16A16_UINT)
        b1 = Buffer(8, HEAP_READBACK)
        shader = hlsl.compile("""
        RWBuffer<uint4> buffer : register(u0);
        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            buffer[tid.x] = uint4(tid.x + 4, 1, 2, 3);
        }
        """)
        compute = Compute(shader, uav=[b0])
        compute.dispatch(1, 1, 1)
        b0.copy_to(b1)
        self.assertEqual(b1.readback(), b'\x04\x00\x01\x00\x02\x00\x03\x00')

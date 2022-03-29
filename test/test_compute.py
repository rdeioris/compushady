import numpy
import struct
import unittest
from compushady import Buffer, Compute, HEAP_UPLOAD, HEAP_READBACK, Texture2D
from compushady.shaders import hlsl
from compushady.formats import R32G32_FLOAT, R32G32B32A32_UINT, R16G16B16A16_FLOAT, R32_UINT
import compushady.config
compushady.config.set_debug(True)


class ComputeTests(unittest.TestCase):

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

    def test_simple_uint(self):
        b0 = Buffer(8, format=R32_UINT)
        b1 = Buffer(8, HEAP_READBACK)
        shader = hlsl.compile("""
        RWBuffer<uint> buffer : register(u0);
        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            buffer[tid.x] = 0xdeadbeef;
        }
        """)
        compute = Compute(shader, uav=[b0])
        compute.dispatch(2, 1, 1)
        b0.copy_to(b1)
        self.assertEqual(struct.unpack('II', b1.readback()),
                         (0xdeadbeef, 0xdeadbeef))

    def test_simple_struct(self):
        b0 = Buffer(16, stride=8)
        b1 = Buffer(b0.size, HEAP_READBACK)
        shader = hlsl.compile("""
        struct Block
        {
            uint a;
            uint b;
        };
        RWStructuredBuffer<Block> buffer : register(u0);
        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            buffer[tid.x].a = 0xdeadbeef;
            buffer[tid.x].b = 0xbeefdead;
        }
        """)
        compute = Compute(shader, uav=[b0])
        compute.dispatch(2, 1, 1)
        b0.copy_to(b1)
        self.assertEqual(struct.unpack('IIII', b1.readback()),
                         (0xdeadbeef, 0xbeefdead, 0xdeadbeef, 0xbeefdead))


    @unittest.skipIf('V3D' in compushady.get_current_device().name, 'float16 not supported on V3D device')
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

    def test_simple_texture2d_fill(self):
        u0 = Texture2D(2, 2, R32_UINT)
        shader = hlsl.compile("""

        RWTexture2D<uint> output : register(u0);

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            output[tid.xy] = 17;
        }
        """)
        compute = Compute(shader, uav=[u0])
        compute.dispatch(1, 1, 1)
        b0 = Buffer(u0.size)
        b1 = Buffer(u0.size, HEAP_READBACK)
        u0.copy_to(b0)
        b0.copy_to(b1)
        self.assertEqual(b1.readback(4), b'\x11\0\0\0')

    def test_texture2d_copy2(self):
        t0 = Texture2D(2, 2, R32_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b0.upload(b'\1\0\0\0')
        b0.copy_to(t0)
        u0 = Texture2D(2, 2, R32_UINT)
        shader = hlsl.compile("""

        Texture2D<uint> input : register(t0);
        RWTexture2D<uint> output : register(u0);

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            output[tid.xy] = input[tid.xy] * 2;
        }
        """)
        compute = Compute(shader, srv=[t0], uav=[u0])
        compute.dispatch(1, 1, 1)
        b0 = Buffer(u0.size, HEAP_READBACK)
        u0.copy_to(b0)
        self.assertEqual(b0.readback(4), b'\2\0\0\0')

    def test_texture2d_copy_constant_buffer(self):
        u0 = Texture2D(2, 2, R32_UINT)
        b0 = Buffer(u0.size, HEAP_UPLOAD)
        b0.upload(b'\1\0\0\0\2\0\0\0\3\0\0\0\4\0\0\0')
        b1 = Buffer(u0.size)
        b0.copy_to(b1)
        shader = hlsl.compile("""

        cbuffer input : register(b0)
        {
            uint a;
            uint b;
            uint c;
            uint d;
        };

        RWTexture2D<uint> output : register(u0);

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            uint value = a;
            if (tid.x == 1 && tid.y == 0)
            {
                value = b;
            }
            else if (tid.x == 0 && tid.y == 1)
            {
                value = c;
            }
            else if (tid.x == 1 && tid.y == 1)
            {
                value = d;
            }
            output[tid.xy] = value;
        }
        """)
        compute = Compute(shader, cbv=[b1], uav=[u0])
        compute.dispatch(2, 2, 1)
        b1 = Buffer(u0.size, HEAP_READBACK)
        u0.copy_to(b1)
        self.assertEqual(b1.readback(4), b'\1\0\0\0')
        self.assertEqual(b1.readback(4, 4), b'\2\0\0\0')
        self.assertEqual(b1.readback(4, u0.row_pitch), b'\3\0\0\0')
        self.assertEqual(b1.readback(4, u0.row_pitch + 4), b'\4\0\0\0')

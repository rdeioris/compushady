import numpy
import struct
import unittest
from compushady import Buffer, Compute, HEAP_UPLOAD, HEAP_READBACK, Texture2D, Texture3D
from compushady.shaders import hlsl
from compushady.formats import (
    R32G32_FLOAT,
    R32G32B32A32_UINT,
    R16G16B16A16_FLOAT,
    R32_UINT,
)
import compushady.config
import platform

compushady.config.set_debug(True)


class ComputeTests(unittest.TestCase):

    def test_simple_fill32(self):
        b0 = Buffer(32, format=R32G32B32A32_UINT)
        b1 = Buffer(32, HEAP_READBACK)
        shader = hlsl.compile(
            """
        RWBuffer<uint4> buffer : register(u0);
        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            buffer[tid.x] = uint4(tid.x + 4, 1, 2, 3);
        }
        """
        )
        compute = Compute(shader, uav=[b0])
        compute.dispatch(2, 1, 1)
        b0.copy_to(b1)
        self.assertEqual(struct.unpack("8I", b1.readback()), (4, 1, 2, 3, 5, 1, 2, 3))

    def test_simple_uint(self):
        b0 = Buffer(8, format=R32_UINT)
        b1 = Buffer(8, HEAP_READBACK)
        shader = hlsl.compile(
            """
        RWBuffer<uint> buffer : register(u0);
        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            buffer[tid.x] = 0xdeadbeef;
        }
        """
        )
        compute = Compute(shader, uav=[b0])
        compute.dispatch(2, 1, 1)
        b0.copy_to(b1)
        self.assertEqual(struct.unpack("II", b1.readback()), (0xDEADBEEF, 0xDEADBEEF))

    def test_simple_struct(self):
        b0 = Buffer(16, stride=8)
        b1 = Buffer(b0.size, HEAP_READBACK)
        shader = hlsl.compile(
            """
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
        """
        )
        compute = Compute(shader, uav=[b0])
        compute.dispatch(2, 1, 1)
        b0.copy_to(b1)
        self.assertEqual(
            struct.unpack("IIII", b1.readback()),
            (0xDEADBEEF, 0xBEEFDEAD, 0xDEADBEEF, 0xBEEFDEAD),
        )

    @unittest.skipIf(
        "V3D" in compushady.get_current_device().name,
        "float16 not supported on V3D device",
    )
    def test_simple_fill_float16(self):
        b0 = Buffer(32, format=R16G16B16A16_FLOAT)
        b1 = Buffer(32, HEAP_READBACK)
        shader = hlsl.compile(
            """
        RWBuffer<float4> buffer : register(u0);
        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            buffer[tid.x] = float4(tid.x, 3, 2, 1);
        }
        """
        )
        compute = Compute(shader, uav=[b0])
        compute.dispatch(4, 1, 1)
        b0.copy_to(b1)
        self.assertTrue(
            numpy.array_equal(
                numpy.frombuffer(b1.readback(), dtype=numpy.float16),
                (0, 3, 2, 1, 1, 3, 2, 1, 2, 3, 2, 1, 3, 3, 2, 1),
            )
        )

    def test_simple_texture2d_fill(self):
        u0 = Texture2D(2, 2, R32_UINT)
        shader = hlsl.compile(
            """

        RWTexture2D<uint> output : register(u0);

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            output[tid.xy] = 17;
        }
        """
        )
        compute = Compute(shader, uav=[u0])
        compute.dispatch(1, 1, 1)
        b0 = Buffer(u0.size)
        b1 = Buffer(u0.size, HEAP_READBACK)
        u0.copy_to(b0)
        b0.copy_to(b1)
        self.assertEqual(b1.readback(4), b"\x11\0\0\0")

    def test_texture2d_copy2(self):
        t0 = Texture2D(2, 2, R32_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b0.upload(b"\1\0\0\0")
        b0.copy_to(t0)
        u0 = Texture2D(2, 2, R32_UINT)
        shader = hlsl.compile(
            """

        Texture2D<uint> input : register(t0);
        RWTexture2D<uint> output : register(u0);

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            output[tid.xy] = input[tid.xy] * 2;
        }
        """
        )
        compute = Compute(shader, srv=[t0], uav=[u0])
        compute.dispatch(1, 1, 1)
        b0 = Buffer(u0.size, HEAP_READBACK)
        u0.copy_to(b0)
        self.assertEqual(b0.readback(4), b"\2\0\0\0")

    def test_texture2d_copy_constant_buffer(self):
        u0 = Texture2D(2, 2, R32_UINT)
        b0 = Buffer(u0.size, HEAP_UPLOAD)
        b0.upload(b"\1\0\0\0\2\0\0\0\3\0\0\0\4\0\0\0")
        b1 = Buffer(u0.size)
        b0.copy_to(b1)
        shader = hlsl.compile(
            """

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
        """
        )
        compute = Compute(shader, cbv=[b1], uav=[u0])
        compute.dispatch(2, 2, 1)
        b1 = Buffer(u0.size, HEAP_READBACK)
        u0.copy_to(b1)
        self.assertEqual(b1.readback(4), b"\1\0\0\0")
        self.assertEqual(b1.readback(4, 4), b"\2\0\0\0")
        self.assertEqual(b1.readback(4, u0.row_pitch), b"\3\0\0\0")
        self.assertEqual(b1.readback(4, u0.row_pitch + 4), b"\4\0\0\0")

    def test_texture3d_depth(self):
        u0 = Texture3D(1, 1, 2, R32_UINT)
        b0 = Buffer(u0.size, HEAP_READBACK)
        shader = hlsl.compile(
            """

        RWTexture3D<uint> output : register(u0);

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            output[tid] = tid.z * 3 + 1;
        }
        """
        )
        compute = Compute(shader, uav=[u0])
        compute.dispatch(1, 1, 2)
        u0.copy_to(b0)
        self.assertEqual(b0.readback(4), b"\1\0\0\0")
        self.assertEqual(b0.readback(4, u0.row_pitch), b"\4\0\0\0")

    def test_indirect(self):
        indirect_buffer = Buffer(4 * 4, format=R32_UINT)
        Compute(
            hlsl.compile(
                """
        RWBuffer<uint> output;

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            output[0] = 1;
            output[1] = 2;
            output[2] = 3;
            output[3] = 4;
        }
        """
            ),
            uav=[indirect_buffer],
        ).dispatch(1, 1, 1)
        output_buffer = Buffer(2 * 3 * 4 * 4, format=R32_UINT)
        Compute(
            hlsl.compile(
                """
        RWBuffer<uint> output;

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            const uint index = tid.z * 3 * 2 + tid.y * 2 + tid.x;
            output[index] = index;
        }
        """
            ),
            uav=[output_buffer],
        ).dispatch_indirect(indirect_buffer, 4)
        readback_buffer = Buffer(output_buffer.size, HEAP_READBACK)
        output_buffer.copy_to(readback_buffer)
        self.assertEqual(
            readback_buffer.readback(2 * 3 * 4 * 4), struct.pack("<24I", *range(0, 24))
        )

    def test_push(self):
        b0 = Buffer(32, format=R32G32B32A32_UINT)
        b1 = Buffer(b0.size, HEAP_READBACK)
        shader = hlsl.compile(
            """
        RWBuffer<uint4> buffer : register(u0);
     
        struct PushConstants
        {
            uint2 values;
        };

        [[vk::push_constant]]
        ConstantBuffer<PushConstants> push_constants;
        

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            uint value = push_constants.values[tid.x];
            buffer[tid.x] = uint4(value, value, value, value);
        }
        """
        )
        compute = Compute(shader, uav=[b0], push_size=8)
        compute.dispatch(3, 1, 1, struct.pack("<II", 100, 200))
        b0.copy_to(b1)
        self.assertEqual(
            struct.unpack("8I", b1.readback(32)),
            (100, 100, 100, 100, 200, 200, 200, 200),
        )

    def test_bindless(self):
        try:
            shader = hlsl.compile(
                """
            [numthreads(1, 1, 1)]
            void main(uint3 tid : SV_DispatchThreadID)
            {
                Buffer<uint> buffer0 = ResourceDescriptorHeap[64 + tid.x];
                RWBuffer<uint> target0 = ResourceDescriptorHeap[64 + 64];
                target0[tid.x] = buffer0[0];
            }
            """,
                "main",
                "cs_6_6",
            )
        except ValueError:
            self.skipTest("shader model 6.6 not supported")

        compute = Compute(shader, bindless=True)

        b_upload = Buffer(4, HEAP_UPLOAD)
        b_output = Buffer(4 * 64, format=R32_UINT)
        b_readback = Buffer(b_output.size, HEAP_READBACK)

        compute.bind_uav(0, b_output)

        for i in range(0, 64):
            b = Buffer(4, format=R32_UINT)
            b_upload.upload(struct.pack("<I", i))
            b_upload.copy_to(b)
            compute.bind_srv(i, b)

        compute.dispatch(64, 1, 1)

        b_output.copy_to(b_readback)

        self.assertEqual(
            struct.unpack("<64I", b_readback.readback(4 * 64)), tuple(range(0, 64))
        )

    @unittest.skipIf(
        platform.system() == "Darwin", "Tests meaningless on Apple platform"
    )
    def test_bindless_legacy(self):
        shader = hlsl.compile(
            """
        Buffer<uint> buffers[];
        RWBuffer<uint> targets[];

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            Buffer<uint> buffer0 = buffers[tid.x];
            targets[0][tid.x] = buffer0[0];
        }
        """
        )

        compute = Compute(shader, bindless=True)

        b_upload = Buffer(4, HEAP_UPLOAD)
        b_output = Buffer(4 * 64, format=R32_UINT)
        b_readback = Buffer(b_output.size, HEAP_READBACK)

        compute.bind_uav(0, b_output)

        for i in range(0, 64):
            b = Buffer(4, format=R32_UINT)
            b_upload.upload(struct.pack("<I", i))
            b_upload.copy_to(b)
            compute.bind_srv(i, b)

        compute.dispatch(64, 1, 1)

        b_output.copy_to(b_readback)

        self.assertEqual(
            struct.unpack("<64I", b_readback.readback(4 * 64)), tuple(range(0, 64))
        )

    @unittest.skipIf(
        platform.system() == "Darwin", "Tests meaningless on Apple platform"
    )
    def test_bindless_legacy_mixed(self):
        shader = hlsl.compile(
            """
        Buffer<uint> buffers[];
        RWBuffer<uint> targets[];

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            Buffer<uint> buffer0 = buffers[tid.x];
            targets[0][tid.x] = buffer0[0];
        }
        """
        )

        b_upload = Buffer(4, HEAP_UPLOAD)
        b_output = Buffer(4 * 64, format=R32_UINT)
        b_readback = Buffer(b_output.size, HEAP_READBACK)

        b0 = Buffer(4, format=R32_UINT)
        b_upload.upload(struct.pack("<I", 0))
        b_upload.copy_to(b0)

        b1 = Buffer(4, format=R32_UINT)
        b_upload.upload(struct.pack("<I", 1))
        b_upload.copy_to(b1)

        compute = Compute(shader, srv=[b0, b1], uav=[b_output], bindless=True)

        for i in range(2, 64):
            b = Buffer(4, format=R32_UINT)
            b_upload.upload(struct.pack("<I", i))
            b_upload.copy_to(b)
            compute.bind_srv(i, b)

        compute.dispatch(64, 1, 1)

        b_output.copy_to(b_readback)

        self.assertEqual(
            struct.unpack("<64I", b_readback.readback(4 * 64)), tuple(range(0, 64))
        )

    @unittest.skipIf(
        platform.system() == "Darwin", "Tests meaningless on Apple platform"
    )
    @unittest.skipIf(
        compushady.get_backend().name == "vulkan", "Tests meaningless on Vulkan backend"
    )
    def test_bindless_legacy_structured(self):
        shader = hlsl.compile(
            """
        struct Data
        {
            uint value;
        };
        StructuredBuffer<Data> buffers[];
        RWStructuredBuffer<Data> targets[];

        [numthreads(1, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            StructuredBuffer<Data> buffer0 = buffers[tid.x];
            targets[0][tid.x].value = buffer0[0].value;
        }
        """
        )

        compute = Compute(shader, bindless=True)

        b_upload = Buffer(4, HEAP_UPLOAD)
        b_output = Buffer(4 * 64, format=R32_UINT)
        b_readback = Buffer(b_output.size, HEAP_READBACK)

        compute.bind_uav(0, b_output)

        for i in range(0, 64):
            b = Buffer(4, format=R32_UINT)
            b_upload.upload(struct.pack("<I", i))
            b_upload.copy_to(b)
            compute.bind_srv(i, b)

        compute.dispatch(64, 1, 1)

        b_output.copy_to(b_readback)

        self.assertEqual(
            struct.unpack("<64I", b_readback.readback(4 * 64)), tuple(range(0, 64))
        )

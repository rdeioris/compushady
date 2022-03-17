import struct
import unittest
from compushady import SHADER_BINARY_TYPE_GLSL, SHADER_BINARY_TYPE_MSL
from compushady.shaders import hlsl
import compushady.config
compushady.config.set_debug(True)


class DXCTests(unittest.TestCase):

    def test_to_glsl(self):
        glsl = hlsl.dxc.compile("""
        Buffer<float4> input : register(t0);
        RWTexture2D<float4> output : register(u0);
        [numthreads(1, 2, 3)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            output[tid.xy] = input[tid.z] * 2;
        }
        """, 'main', SHADER_BINARY_TYPE_GLSL)
        self.assertTrue(
            b'layout(binding = 1024) uniform samplerBuffer _input;' in glsl)
        self.assertTrue(
            b'layout(binding = 2048, rgba32f) uniform writeonly image2D _output;' in glsl)

    def test_to_msl(self):
        msl, grid = hlsl.dxc.compile("""
        struct Data
        {
            uint a;
            uint b;
        };
        Buffer<float4> input : register(t0);
        Texture2D<float4> input2 : register(t1);
        RWTexture2D<float4> output : register(u0);
        ConstantBuffer<Data> data : register(b0);
        StructuredBuffer<Data> data2 : register(t5);
        RWStructuredBuffer<Data> data3 : register(u5);
        StructuredBuffer<Data> data4 : register(t4);
        RWBuffer<float4> output2 : register(u3);
        [numthreads(1, 2, 3)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            output[tid.xy] = input[tid.z] * 2 + input2[tid.xy] + data.a + data2[0].b + data4[0].a;
            output2[tid.z].r = data.b;
            data3[1].b = 0;
        }
        """, 'main', SHADER_BINARY_TYPE_MSL)
        self.assertTrue(b'_input [[texture(0)]]' in msl)
        self.assertTrue(b'input2 [[texture(1)]]' in msl)
        self.assertTrue(b'_output [[texture(2)]]' in msl)
        self.assertTrue(b'data [[buffer(0)]]' in msl)
        self.assertTrue(b'data2 [[buffer(2)]]' in msl)
        self.assertTrue(b'data3 [[buffer(3)]]' in msl)
        self.assertTrue(b'data4 [[buffer(1)]]' in msl)
        self.assertTrue(b'output2 [[texture(3)]]' in msl)
        self.assertEqual(grid, (1, 2, 3))

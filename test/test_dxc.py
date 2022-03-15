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
            b'layout(binding = 32) uniform samplerBuffer _input;' in glsl)
        self.assertTrue(
            b'layout(binding = 64, rgba32f) uniform writeonly image2D _output;' in glsl)

    def test_to_msl(self):
        msl = hlsl.dxc.compile("""
        Buffer<float4> input : register(t0);
        RWTexture2D<float4> output : register(u0);
        [numthreads(1, 2, 3)]
        void main(uint3 tid : SV_DispatchThreadID)
        {
            output[tid.xy] = input[tid.z] * 2;
        }
        """, 'main', SHADER_BINARY_TYPE_MSL)
        self.assertTrue(b'_input [[texture(32)]]' in msl)
        self.assertTrue(b'_output [[texture(64)]]' in msl)

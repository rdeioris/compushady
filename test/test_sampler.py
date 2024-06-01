import unittest
from compushady import (
    Sampler,
    SAMPLER_FILTER_POINT,
    SAMPLER_ADDRESS_MODE_CLAMP,
    SAMPLER_ADDRESS_MODE_WRAP,
    SAMPLER_ADDRESS_MODE_MIRROR,
    Compute,
    Texture2D,
    Buffer,
    HEAP_UPLOAD,
    HEAP_READBACK,
)
from compushady.formats import R8G8B8A8_UNORM
from compushady.shaders import hlsl
import compushady.config
import struct

compushady.config.set_debug(True)


class SamplerTests(unittest.TestCase):
    def test_sampler_simple(self):
        t0 = Texture2D(2, 2, R8G8B8A8_UNORM)
        t0_upload = Buffer(t0.size, HEAP_UPLOAD)
        t0_upload.upload2d(
            b"\xff\xff\xff\xff\xaa\xaa\xaa\xaa\xbb\xbb\xbb\xbb\xcc\xcc\xcc\xcc",
            t0.row_pitch,
            t0.width,
            t0.height,
            4,
        )
        t0_upload.copy_to(t0)
        t1 = Texture2D(2, 2, R8G8B8A8_UNORM)
        t1_readback = Buffer(t1.size, HEAP_READBACK)
        sampler = Sampler(
            SAMPLER_ADDRESS_MODE_CLAMP,
            SAMPLER_ADDRESS_MODE_CLAMP,
            SAMPLER_ADDRESS_MODE_CLAMP,
            SAMPLER_FILTER_POINT,
            SAMPLER_FILTER_POINT,
        )

        shader = hlsl.compile(
            """
SamplerState sampler0 : register(s0);
Texture2D<float4> source : register(t0);
RWTexture2D<float4> target : register(u0);

[numthreads(1,1,1)]
void main(int3 tid : SV_DispatchThreadID)
{
    target[tid.xy] = source.SampleLevel(sampler0, float2(float(tid.x), float(tid.y)), 0);
}
"""
        )

        compute = Compute(shader, srv=[t0], uav=[t1], samplers=[sampler])
        compute.dispatch(2, 2, 1)
        t1.copy_to(t1_readback)
        p00, p10, p01, p11 = struct.unpack(
            "<IIII", t1_readback.readback2d(t1.row_pitch, t1.width, t1.height, 4)
        )
        self.assertEqual(p00, 0xFFFFFFFF)
        self.assertEqual(p10, 0xAAAAAAAA)
        self.assertEqual(p01, 0xBBBBBBBB)
        self.assertEqual(p11, 0xCCCCCCCC)

    def test_sampler_clamp(self):
        t0 = Texture2D(2, 2, R8G8B8A8_UNORM)
        t0_upload = Buffer(t0.size, HEAP_UPLOAD)
        t0_upload.upload2d(
            b"\xff\xff\xff\xff\xaa\xaa\xaa\xaa\xbb\xbb\xbb\xbb\xcc\xcc\xcc\xcc",
            t0.row_pitch,
            t0.width,
            t0.height,
            4,
        )
        t0_upload.copy_to(t0)
        t1 = Texture2D(2, 2, R8G8B8A8_UNORM)
        t1_readback = Buffer(t1.size, HEAP_READBACK)
        sampler = Sampler(
            SAMPLER_ADDRESS_MODE_CLAMP,
            SAMPLER_ADDRESS_MODE_CLAMP,
            SAMPLER_ADDRESS_MODE_CLAMP,
            SAMPLER_FILTER_POINT,
            SAMPLER_FILTER_POINT,
        )

        shader = hlsl.compile(
            """
SamplerState sampler0 : register(s0);
Texture2D<float4> source : register(t0);
RWTexture2D<float4> target : register(u0);

[numthreads(1,1,1)]
void main(int3 tid : SV_DispatchThreadID)
{
    target[tid.xy] = source.SampleLevel(sampler0, float2(float(tid.x) + 1, float(tid.y) + 1), 0);
}
"""
        )

        compute = Compute(shader, srv=[t0], uav=[t1], samplers=[sampler])
        compute.dispatch(2, 2, 1)
        t1.copy_to(t1_readback)
        p00, p10, p01, p11 = struct.unpack(
            "<IIII", t1_readback.readback2d(t1.row_pitch, t1.width, t1.height, 4)
        )
        self.assertEqual(p00, 0xCCCCCCCC)
        self.assertEqual(p10, 0xCCCCCCCC)
        self.assertEqual(p01, 0xCCCCCCCC)
        self.assertEqual(p11, 0xCCCCCCCC)

    def test_sampler_wrap(self):
        t0 = Texture2D(2, 2, R8G8B8A8_UNORM)
        t0_upload = Buffer(t0.size, HEAP_UPLOAD)
        t0_upload.upload2d(
            b"\xff\xff\xff\xff\xaa\xaa\xaa\xaa\xbb\xbb\xbb\xbb\xcc\xcc\xcc\xcc",
            t0.row_pitch,
            t0.width,
            t0.height,
            4,
        )
        t0_upload.copy_to(t0)
        t1 = Texture2D(2, 2, R8G8B8A8_UNORM)
        t1_readback = Buffer(t1.size, HEAP_READBACK)
        sampler = Sampler(
            SAMPLER_ADDRESS_MODE_WRAP,
            SAMPLER_ADDRESS_MODE_WRAP,
            SAMPLER_ADDRESS_MODE_WRAP,
            SAMPLER_FILTER_POINT,
            SAMPLER_FILTER_POINT,
        )

        shader = hlsl.compile(
            """
SamplerState sampler0 : register(s0);
Texture2D<float4> source : register(t0);
RWTexture2D<float4> target : register(u0);

[numthreads(1,1,1)]
void main(int3 tid : SV_DispatchThreadID)
{
    target[tid.xy] = source.SampleLevel(sampler0, float2(float(tid.x) + 0.51, float(tid.y) + 0.51), 0);
}
"""
        )

        compute = Compute(shader, srv=[t0], uav=[t1], samplers=[sampler])
        compute.dispatch(2, 2, 1)
        t1.copy_to(t1_readback)
        p00, p10, p01, p11 = struct.unpack(
            "<IIII", t1_readback.readback2d(t1.row_pitch, t1.width, t1.height, 4)
        )
        self.assertEqual(p00, 0xCCCCCCCC)
        self.assertEqual(p10, 0xCCCCCCCC)
        self.assertEqual(p01, 0xCCCCCCCC)
        self.assertEqual(p11, 0xCCCCCCCC)

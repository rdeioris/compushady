import unittest
from compushady import (
    Sampler,
    SAMPLER_FILTER_POINT,
    SAMPLER_ADDRESS_MODE_CLAMP,
    SAMPLER_ADDRESS_MODE_WRAP,
    SAMPLER_ADDRESS_MODE_MIRROR,
    Compute,
    Texture1D,
    Texture2D,
    Buffer,
    HEAP_UPLOAD,
    HEAP_READBACK,
    HEAP_DEFAULT,
)
from compushady.formats import R8G8B8A8_UNORM, R32_UINT
from compushady.shaders import wgsl
import compushady.config
import struct

compushady.config.set_debug(True)


class WGSLTests(unittest.TestCase):
    def test_sampler_simple(self):
        b0 = Buffer(4, HEAP_UPLOAD)
        b0.upload(struct.pack("<f", 1))
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

        shader = wgsl.compile(
            """
@group(0) @binding(0)
var sampler0: sampler;

@group(0) @binding(1)   
var source0: texture_2d<f32>;

@group(0) @binding(2)
var target0: texture_storage_2d<rgba8unorm, write>;

@group(0) @binding(3)
var<uniform> multiplier : f32;

@compute @workgroup_size(1, 1, 1)
fn main(@builtin(global_invocation_id) tid: vec3<u32>)
{
    let color : vec4<f32> = textureSampleLevel(source0, sampler0, vec2<f32>(f32(tid.x), f32(tid.y)), 0.0);
    textureStore(target0, tid.xy, color * multiplier);
}
"""
        )

        compute = Compute(shader, cbv=[b0], srv=[t0], uav=[t1], samplers=[sampler])
        compute.dispatch(2, 2, 1)
        t1.copy_to(t1_readback)
        p00, p10, p01, p11 = struct.unpack(
            "<IIII", t1_readback.readback2d(t1.row_pitch, t1.width, t1.height, 4)
        )
        self.assertEqual(p00, 0xFFFFFFFF)
        self.assertEqual(p10, 0xAAAAAAAA)
        self.assertEqual(p01, 0xBBBBBBBB)
        self.assertEqual(p11, 0xCCCCCCCC)

    def test_buffer(self):
        u0_upload = Buffer(4, HEAP_UPLOAD)
        u0_upload.upload(struct.pack("<I", 3))

        u0 = Buffer(u0_upload.size, HEAP_DEFAULT, stride=4)
        u0_upload.copy_to(u0)

        u0_readback = Buffer(u0.size, HEAP_READBACK)

        shader = wgsl.compile(
            """
@group(0) @binding(0)
var<storage, read_write> output0 : array<u32>;

@compute @workgroup_size(1, 1, 1)
fn main() {
    output0[0] *= 3u;
}
            """
        )
        compute = Compute(shader, uav=[u0])
        compute.dispatch(1, 1, 1)

        u0.copy_to(u0_readback)
        self.assertEqual(struct.unpack("<I", u0_readback.readback(4))[0], 9)

    def test_texture_storage(self):
        u0 = Texture1D(1, R32_UINT)
        u0_upload = Buffer(u0.size, HEAP_UPLOAD)
        u0_upload.upload(struct.pack("<I", 3))
        u0_upload.copy_to(u0)

        u0_readback = Buffer(u0.size, HEAP_READBACK)

        shader = wgsl.compile(
            """
@group(0) @binding(0)
var output0 : texture_storage_1d<r32uint, read_write>;

@compute @workgroup_size(1, 1, 1)
fn main() {
    textureStore(output0, 0, textureLoad(output0, 0) * 3);
}
            """
        )
        compute = Compute(shader, uav=[u0])
        compute.dispatch(1, 1, 1)

        u0.copy_to(u0_readback)
        self.assertEqual(struct.unpack("<I", u0_readback.readback(4))[0], 9)

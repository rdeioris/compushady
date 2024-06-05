import unittest
from compushady import Texture1D, Buffer, HEAP_UPLOAD, HEAP_READBACK
from compushady.formats import (
    R8G8B8A8_UINT,
    get_pixel_size,
    R16G16B16A16_FLOAT,
    R32_UINT,
)
import compushady.config
import struct

compushady.config.set_debug(True)


class TextureArrayTests(unittest.TestCase):

    def test_simple(self):
        t0 = Texture1D(2, R32_UINT, slices=4)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size, HEAP_READBACK)

        b0.upload(struct.pack("<II", 1, 2))
        b0.copy_to(t0, dst_slice=0)
        b0.upload(struct.pack("<II", 3, 4))
        b0.copy_to(t0, dst_slice=1)
        b0.upload(struct.pack("<II", 5, 6))
        b0.copy_to(t0, dst_slice=2)
        b0.upload(struct.pack("<II", 7, 8))
        b0.copy_to(t0, dst_slice=3)

        t0.copy_to(b1, src_slice=0)
        self.assertEqual(struct.unpack("<II", b1.readback(8)), (1, 2))
        t0.copy_to(b1, src_slice=1)
        self.assertEqual(struct.unpack("<II", b1.readback(8)), (3, 4))
        t0.copy_to(b1, src_slice=2)
        self.assertEqual(struct.unpack("<II", b1.readback(8)), (5, 6))
        t0.copy_to(b1, src_slice=3)
        self.assertEqual(struct.unpack("<II", b1.readback(8)), (7, 8))

    def test_texture_to_texture(self):
        t0 = Texture1D(2, R32_UINT, slices=4)
        t1 = Texture1D(t0.width, R32_UINT, slices=t0.slices)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size, HEAP_READBACK)

        b0.upload(struct.pack("<II", 1, 2))
        b0.copy_to(t0, dst_slice=0)
        b0.upload(struct.pack("<II", 3, 4))
        b0.copy_to(t0, dst_slice=1)
        b0.upload(struct.pack("<II", 5, 6))
        b0.copy_to(t0, dst_slice=2)
        b0.upload(struct.pack("<II", 7, 8))
        b0.copy_to(t0, dst_slice=3)

        t0.copy_to(t1, src_slice=0, dst_slice=0)
        t0.copy_to(t1, src_slice=1, dst_slice=1)
        t0.copy_to(t1, src_slice=2, dst_slice=2)
        t0.copy_to(t1, src_slice=3, dst_slice=3)

        t1.copy_to(b1, src_slice=0)
        self.assertEqual(struct.unpack("<II", b1.readback(8)), (1, 2))
        t1.copy_to(b1, src_slice=1)
        self.assertEqual(struct.unpack("<II", b1.readback(8)), (3, 4))
        t1.copy_to(b1, src_slice=2)
        self.assertEqual(struct.unpack("<II", b1.readback(8)), (5, 6))
        t1.copy_to(b1, src_slice=3)
        self.assertEqual(struct.unpack("<II", b1.readback(8)), (7, 8))

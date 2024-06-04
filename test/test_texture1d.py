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


class Texture1DTests(unittest.TestCase):

    def test_simple_upload(self):
        t0 = Texture1D(2, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size)
        b2 = Buffer(t0.size, HEAP_READBACK)
        b0.upload(b"\xDE\xAD\xBE\xEF" * 2)
        b0.copy_to(t0)
        t0.copy_to(b1)
        b1.copy_to(b2)
        self.assertEqual(b2.readback(4), b"\xDE\xAD\xBE\xEF")

    def test_zero_width(self):
        self.assertRaises(ValueError, Texture1D, 0, R16G16B16A16_FLOAT)

    def test_simple_upload_float(self):
        t0 = Texture1D(2, R16G16B16A16_FLOAT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size, HEAP_READBACK)
        b0.upload(struct.pack("4f", 1, 2, 3, 4))
        b0.copy_to(t0)
        t0.copy_to(b1)
        self.assertEqual(b1.readback(16), struct.pack("4f", 1, 2, 3, 4))

    def test_copy_with_offset(self):
        t0 = Texture1D(2, R32_UINT)
        b0 = Buffer(t0.size * 2, HEAP_UPLOAD)
        b0.upload(struct.pack("<III", 1, 2, 3))
        b0.copy_to(t0, size=t0.size)
        b0.copy_to(t0, size=t0.size, src_offset=4)
        b1 = Buffer(t0.size, HEAP_READBACK)
        t0.copy_to(b1)
        self.assertEqual(b1.readback(8), struct.pack("<II", 2, 3))

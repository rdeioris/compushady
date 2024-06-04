import unittest
from compushady import Texture3D, Texture2D, Buffer, HEAP_UPLOAD, HEAP_READBACK
from compushady.formats import R8G8B8A8_UINT, get_pixel_size, R16G16B16A16_FLOAT
import compushady.config
import struct

compushady.config.set_debug(True)


class Texture3DTests(unittest.TestCase):

    def test_simple_upload(self):
        t0 = Texture3D(2, 2, 2, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size)
        b2 = Buffer(t0.size, HEAP_READBACK)
        b0.upload(b"\1\2\3\4\5\6\7\x08")
        for y in range(1, t0.height * t0.depth):
            b0.upload(b"\xDE\xAD\xBE\xEF" * 2, offset=t0.row_pitch * y)
        b0.copy_to(t0)
        t0.copy_to(b1)
        b1.copy_to(b2)
        self.assertEqual(
            b2.readback(4, offset=t0.row_pitch * (t0.height + 1)), b"\xDE\xAD\xBE\xEF"
        )

    def test_simple_upload2d(self):
        t0 = Texture3D(2, 2, 2, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size, HEAP_READBACK)
        b0.upload2d(
            b"\xDE\xAD\xBE\xEF" * 2,
            t0.row_pitch,
            t0.width,
            t0.height,
            get_pixel_size(R8G8B8A8_UINT),
        )
        b0.copy_to(t0)
        t0.copy_to(b1)
        self.assertEqual(b1.readback(4, offset=4), b"\xDE\xAD\xBE\xEF")

    def test_simple_upload_float(self):
        t0 = Texture3D(2, 2, 2, R16G16B16A16_FLOAT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size, HEAP_READBACK)
        b0.upload(struct.pack("4f", 1, 2, 3, 4))
        b0.copy_to(t0)
        t0.copy_to(b1)
        self.assertEqual(b1.readback(16), struct.pack("4f", 1, 2, 3, 4))

    def test_copy_2d_to_3d(self):
        t0 = Texture2D(256, 256, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b0.upload(struct.pack("<65536I", *range(0, 65536)))
        b0.copy_to(t0)
        t1 = Texture3D(256, 256, 2, R8G8B8A8_UINT)
        b1 = Buffer(t1.size, HEAP_UPLOAD)
        b1.upload(bytes(b1.size))
        b1.copy_to(t1)
        b2 = Buffer(t1.size, HEAP_READBACK)
        t1.copy_to(b2)
        self.assertEqual(b2.readback(), bytes(b2.size))
        t0.copy_to(t1, dst_z=1)
        t1.copy_to(b2)
        self.assertEqual(b2.readback(1024), bytes(1024))
        self.assertNotEqual(b2.readback(), bytes(b2.size))

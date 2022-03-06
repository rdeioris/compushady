import unittest
from compushady import Texture2D, Buffer, HEAP_UPLOAD, HEAP_READBACK
from compushady.formats import R8G8B8A8_UINT, R8G8B8A8_UNORM, get_pixel_size
import compushady.config
compushady.config.set_debug(True)


class Texture2DTests(unittest.TestCase):

    def test_simple_upload(self):
        t0 = Texture2D(2, 2, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size)
        b2 = Buffer(t0.size, HEAP_READBACK)
        for y in range(0, t0.height):
            b0.upload(b'\xDE\xAD\xBE\xEF' * 2, offset=t0.row_pitch * y)
        b0.copy_to(t0)
        t0.copy_to(b1)
        b1.copy_to(b2)
        self.assertEqual(b2.readback(4), b'\xDE\xAD\xBE\xEF')

    def test_simple_upload2d(self):
        t0 = Texture2D(2, 2, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size, HEAP_READBACK)
        b2 = Buffer(t0.size)
        b0.upload2d(b'\xDE\xAD\xBE\xEF', t0.row_pitch, t0.width,
                    t0.height, get_pixel_size(R8G8B8A8_UINT))
        b0.copy_to(t0)
        t0.copy_to(b2)
        b2.copy_to(b1)
        self.assertEqual(b1.readback(4), b'\xDE\xAD\xBE\xEF')

    def test_simple_copy(self):
        t0 = Texture2D(4, 4, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b0.upload(b'\1\2\3\4')
        b0.copy_to(t0)
        t1 = Texture2D(4, 4, R8G8B8A8_UNORM)
        t0.copy_to(t1)
        b1 = Buffer(t0.size)
        b2 = Buffer(t0.size, HEAP_READBACK)
        t1.copy_to(b1)
        b1.copy_to(b2)
        self.assertEqual(b2.readback(4), b'\1\2\3\4')

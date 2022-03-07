import unittest
from compushady import Texture3D, Buffer, HEAP_UPLOAD, HEAP_READBACK
from compushady.formats import R8G8B8A8_UINT, get_pixel_size
import compushady.config
compushady.config.set_debug(True)


class Texture3DTests(unittest.TestCase):

    def test_simple_upload(self):
        t0 = Texture3D(2, 2, 2, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size)
        b2 = Buffer(t0.size, HEAP_READBACK)
        b0.upload(b'\1\2\3\4\5\6\7\x08')
        for y in range(1, t0.height * t0.depth):
            b0.upload(b'\xDE\xAD\xBE\xEF' * 2, offset=t0.row_pitch * y)
        b0.copy_to(t0)
        t0.copy_to(b1)
        b1.copy_to(b2)
        self.assertEqual(b2.readback(4, offset=t0.row_pitch *
                         (t0.height + 1)), b'\xDE\xAD\xBE\xEF')

    def test_simple_upload2d(self):
        t0 = Texture3D(2, 2, 2, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size, HEAP_READBACK)
        b0.upload2d(b'\xDE\xAD\xBE\xEF' * 2, t0.row_pitch, t0.width,
                    t0.height, get_pixel_size(R8G8B8A8_UINT))
        b0.copy_to(t0)
        t0.copy_to(b1)
        self.assertEqual(b1.readback(4, offset=4), b'\xDE\xAD\xBE\xEF')

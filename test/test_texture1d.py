import unittest
from compushady import Texture1D, Buffer, HEAP_UPLOAD, HEAP_READBACK
from compushady.formats import R8G8B8A8_UINT, get_pixel_size
import compushady.config
compushady.config.set_debug(True)


class Texture1DTests(unittest.TestCase):

    def test_simple_upload(self):
        t0 = Texture1D(2, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size)
        b2 = Buffer(t0.size, HEAP_READBACK)
        b0.upload(b'\xDE\xAD\xBE\xEF' * 2)
        b0.copy_to(t0)
        t0.copy_to(b1)
        b1.copy_to(b2)
        self.assertEqual(b2.readback(4), b'\xDE\xAD\xBE\xEF')

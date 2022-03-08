import unittest
from compushady import Buffer, HEAP_UPLOAD, HEAP_READBACK, BufferException, get_current_device
import compushady.config
compushady.config.set_debug(True)


class BufferTests(unittest.TestCase):

    def test_simple_upload(self):
        b0 = Buffer(8, HEAP_UPLOAD)
        b1 = Buffer(8, HEAP_READBACK)
        b0.upload(b'hello!!!')
        b0.copy_to(b1)
        self.assertEqual(b1.readback(), b'hello!!!')

    def test_simple_upload_and_copy(self):
        b0 = Buffer(8, HEAP_UPLOAD)
        b1 = Buffer(8)
        b2 = Buffer(8, HEAP_READBACK)
        b0.upload(b'hello!!!')
        b0.copy_to(b1)
        b1.copy_to(b2)
        self.assertEqual(b2.readback(), b'hello!!!')

    def test_empty_buffer(self):
        self.assertRaises(BufferException, Buffer, 0)

    def test_multiple_copy(self):
        b0 = Buffer(8, HEAP_UPLOAD)
        b1 = Buffer(8)
        b2 = Buffer(8)
        b3 = Buffer(8, HEAP_READBACK)
        b0.upload(b'hello!!!')
        b0.copy_to(b1)
        b1.copy_to(b2)
        b2.copy_to(b1)
        b1.copy_to(b2)
        b1.copy_to(b3)
        self.assertEqual(b3.readback(), b'hello!!!')

    def test_simple_readback_to_buffer(self):
        b0 = Buffer(8, HEAP_UPLOAD)
        b1 = Buffer(8, HEAP_READBACK)
        b0.upload(b'hello!!!')
        b0.copy_to(b1)
        data = bytearray(8)
        b1.readback(data)
        self.assertEqual(data, b'hello!!!')

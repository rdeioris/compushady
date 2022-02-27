import unittest
from compushady import Buffer, HEAP_UPLOAD, HEAP_READBACK, BufferException


class BufferTests(unittest.TestCase):

    def test_simple_upload(self):
        b0 = Buffer(8, HEAP_UPLOAD)
        b0.upload(b'hello!!!')
        self.assertEqual(b0.readback(), b'hello!!!')

    def test_simple_upload_and_copy(self):
        b0 = Buffer(8, HEAP_UPLOAD)
        b1 = Buffer(8, HEAP_READBACK)
        b0.upload(b'hello!!!')
        b0.copy_to(b1)
        self.assertEqual(b0.readback(), b1.readback())

    def test_empty_buffer(self):
        self.assertRaises(BufferException, Buffer, 0)

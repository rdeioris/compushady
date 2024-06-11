import unittest
from compushady import (
    Buffer,
    Heap,
    HEAP_DEFAULT,
    HEAP_UPLOAD,
    HEAP_READBACK,
    BufferException,
    get_current_device,
)
import compushady.config

compushady.config.set_debug(True)


class BufferTests(unittest.TestCase):

    def test_simple_upload(self):
        b0 = Buffer(8, HEAP_UPLOAD)
        b1 = Buffer(8, HEAP_READBACK)
        b0.upload(b"hello!!!")
        b0.copy_to(b1)
        self.assertEqual(b1.readback(), b"hello!!!")

    def test_simple_upload_and_copy(self):
        b0 = Buffer(8, HEAP_UPLOAD)
        b1 = Buffer(8)
        b2 = Buffer(8, HEAP_READBACK)
        b0.upload(b"hello!!!")
        b0.copy_to(b1)
        b1.copy_to(b2)
        self.assertEqual(b2.readback(), b"hello!!!")

    def test_empty_buffer(self):
        self.assertRaises(BufferException, Buffer, 0)

    def test_multiple_copy(self):
        b0 = Buffer(8, HEAP_UPLOAD)
        b1 = Buffer(8)
        b2 = Buffer(8)
        b3 = Buffer(8, HEAP_READBACK)
        b0.upload(b"hello!!!")
        b0.copy_to(b1)
        b1.copy_to(b2)
        b2.copy_to(b1)
        b1.copy_to(b2)
        b1.copy_to(b3)
        self.assertEqual(b3.readback(), b"hello!!!")

    def test_simple_readback_to_buffer(self):
        b0 = Buffer(8, HEAP_UPLOAD)
        b1 = Buffer(8, HEAP_READBACK)
        b0.upload(b"hello!!!")
        b0.copy_to(b1)
        data = bytearray(8)
        b1.readback(data)
        self.assertEqual(data, b"hello!!!")

    def test_upload_chunked(self):
        b0 = Buffer(8, HEAP_UPLOAD)
        b1 = Buffer(8, HEAP_READBACK)
        b0.upload_chunked(b"\xaa\xbb\x11\x22", 2, b"\xee\xff")
        b0.copy_to(b1)
        self.assertEqual(b1.readback(), b"\xaa\xbb\xee\xff\x11\x22\xee\xff")

    def test_copy_size(self):
        b0 = Buffer(256, HEAP_UPLOAD)
        b1 = Buffer(64)
        b2 = Buffer(32, HEAP_READBACK)
        b0.upload(b"hello world")
        b0.copy_to(b1, size=32)
        b1.copy_to(b2, size=11)
        self.assertEqual(b2.readback(11), b"hello world")

    def test_copy_offset(self):
        b0 = Buffer(256, HEAP_UPLOAD)
        b1 = Buffer(64)
        b2 = Buffer(32, HEAP_READBACK)
        b0.upload(b"hello world")
        b0.copy_to(b1, size=32, dst_offset=32)
        b1.copy_to(b2, size=11, src_offset=32)
        self.assertEqual(b2.readback(11), b"hello world")

    def test_copy_bigger_size(self):
        b1 = Buffer(64)
        b2 = Buffer(32)
        self.assertRaises(ValueError, b1.copy_to, b2)

    def test_copy_bigger_size_with_offset(self):
        b1 = Buffer(64)
        b2 = Buffer(64)
        self.assertRaises(ValueError, b1.copy_to, b2, 64, 32, 0)

    def test_copy_bigger_size_with_offset2(self):
        b1 = Buffer(64)
        b2 = Buffer(64)
        self.assertRaises(ValueError, b1.copy_to, b2, 64, 0, 32)

    def test_copy_bigger_size_with_size(self):
        b1 = Buffer(64)
        b2 = Buffer(64)
        self.assertRaises(ValueError, b1.copy_to, b2, 65)

    def test_sparse(self):
        heap = Heap(HEAP_DEFAULT, 1024 * 1024)
        b0 = Buffer(1024 * 1024 * 2, sparse=True)

        b_upload = Buffer(4, HEAP_UPLOAD)
        b_readback = Buffer(4, HEAP_READBACK)

        b_upload.upload(b"\xff\xee\xdd\xaa")

        b0.bind_tile(0, 0, 0, heap)
        b0.bind_tile(1, 0, 0, heap)
        b0.bind_tile(2, 0, 0, heap)

        b_upload.copy_to(b0, size=4, dst_offset=b0.tile_width * 2)

        b0.copy_to(b_readback, size=4, src_offset=b0.tile_width * 2)
        self.assertEqual(b_readback.readback(4), b"\xff\xee\xdd\xaa")

        b0.copy_to(b_readback, size=4, src_offset=b0.tile_width)
        self.assertEqual(b_readback.readback(4), b"\xff\xee\xdd\xaa")

        b0.copy_to(b_readback, size=4, src_offset=0)
        self.assertEqual(b_readback.readback(4), b"\xff\xee\xdd\xaa")

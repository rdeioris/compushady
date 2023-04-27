import unittest
from compushady import (
    Heap,
    Buffer,
    BufferException,
    HEAP_UPLOAD,
    HEAP_READBACK,
    HEAP_DEFAULT,
    HeapException,
    get_current_device,
)
import compushady.config

compushady.config.set_debug(True)


class HeapTests(unittest.TestCase):
    def test_heap_size(self):
        heap = Heap(HEAP_DEFAULT, 256 * 1024)
        self.assertEqual(heap.size, 256 * 1024)

    def test_heap_size_zero(self):
        self.assertRaises(HeapException, Heap, HEAP_DEFAULT, 0)

    def test_heap_type(self):
        heap = Heap(HEAP_DEFAULT, 1024)
        self.assertEqual(heap.heap_type, HEAP_DEFAULT)
        heap = Heap(HEAP_READBACK, 1024)
        self.assertEqual(heap.heap_type, HEAP_READBACK)
        heap = Heap(HEAP_UPLOAD, 1024)
        self.assertEqual(heap.heap_type, HEAP_UPLOAD)

    def test_heap_unknown(self):
        self.assertRaises(HeapException, Heap, 9999, 1024)

    def test_heap_buffer(self):
        heap = Heap(HEAP_DEFAULT, 64 * 1024)
        buffer = Buffer(size=64 * 1024, heap_type=HEAP_DEFAULT, heap=heap)
        self.assertEqual(buffer.size, heap.size)

    def test_heap_buffer_alias(self):
        heap_upload = Heap(HEAP_UPLOAD, 1024)
        heap_readback = Heap(HEAP_READBACK, 1024)
        buffer0 = Buffer(size=1024, heap_type=HEAP_UPLOAD, heap=heap_upload)
        buffer1 = Buffer(size=1024, heap_type=HEAP_UPLOAD, heap=heap_upload)
        buffer0.upload(b"\x01", offset=0)
        buffer1.upload(b"\x02", offset=1)
        buffer2 = Buffer(size=1024, heap_type=HEAP_READBACK, heap=heap_readback)
        buffer1.copy_to(buffer2)
        self.assertEqual(buffer2.readback(2), b"\x01\x02")

    def test_heap_buffer_overlap(self):
        heap_upload = Heap(HEAP_UPLOAD, 1024)
        heap_readback = Heap(HEAP_READBACK, 1024)
        buffer0 = Buffer(size=1024, heap_type=HEAP_UPLOAD, heap=heap_upload)
        buffer1 = Buffer(size=1024, heap_type=HEAP_UPLOAD, heap=heap_upload)
        buffer0.upload(b"\x01", offset=0)
        buffer1.upload(b"\x02\x03", offset=0)
        buffer2 = Buffer(size=1024, heap_type=HEAP_READBACK, heap=heap_readback)
        buffer1.copy_to(buffer2)
        self.assertEqual(buffer2.readback(2), b"\x02\x03")

    def test_heap_buffer_append(self):
        heap_upload = Heap(HEAP_UPLOAD, 256 * 1024)
        heap_readback = Heap(HEAP_READBACK, 256 * 1024)
        buffer0 = Buffer(
            size=256, heap_type=HEAP_UPLOAD, heap=heap_upload, heap_offset=64 * 1024
        )
        buffer1 = Buffer(
            size=256, heap_type=HEAP_UPLOAD, heap=heap_upload, heap_offset=128 * 1024
        )
        buffer_all = Buffer(
            size=256 * 1024, heap_type=HEAP_UPLOAD, heap=heap_upload, heap_offset=0
        )
        buffer0.upload(b"\x01\x02", offset=0)
        buffer1.upload(b"\x03\x04", offset=0)
        buffer2 = Buffer(size=256 * 1024, heap_type=HEAP_READBACK, heap=heap_readback)
        buffer_all.copy_to(buffer2)
        self.assertEqual(buffer2.readback(2, offset=64 * 1024), b"\x01\x02")
        self.assertEqual(buffer2.readback(2, offset=128 * 1024), b"\x03\x04")

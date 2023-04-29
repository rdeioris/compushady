import unittest
from compushady import (
    Heap,
    DeviceException,
    Buffer,
    BufferException,
    HEAP_UPLOAD,
    HEAP_READBACK,
    HEAP_DEFAULT,
    HeapException,
)
import compushady.config

compushady.config.set_debug(True)


class MultiGPUTests(unittest.TestCase):
    def setUp(self):
        self.device0 = compushady.get_discovered_devices()[0]
        self.device1 = compushady.get_discovered_devices()[1]

    def test_buffer_device(self):
        buffer0 = Buffer(1024, device=self.device0)
        buffer1 = Buffer(1024, device=self.device1)
        self.assertNotEqual(buffer0.device, buffer1.device)

    def test_buffer_copy(self):
        buffer0 = Buffer(1024, device=self.device0)
        buffer1 = Buffer(1024, device=self.device1)
        self.assertRaises(DeviceException, buffer0.copy_to, buffer1)

    def test_buffer_staging(self):
        buffer0 = Buffer(1024, heap_type=HEAP_UPLOAD, device=self.device0)
        buffer1 = Buffer(1024, heap_type=HEAP_UPLOAD, device=self.device1)

        buffer0.upload(b"test")
        staging0 = Buffer(1024, heap_type=HEAP_READBACK, device=self.device0)
        buffer0.copy_to(staging0)

        buffer1.upload(staging0.readback(4))
        staging1 = Buffer(1024, heap_type=HEAP_READBACK, device=self.device1)
        buffer1.copy_to(staging1)

        self.assertEqual(staging1.readback(4), b"test")

    def test_heap_device(self):
        heap0 = Buffer(1024, device=self.device0)
        heap1 = Buffer(1024, device=self.device1)
        self.assertNotEqual(heap0.device, heap1.device)

    def test_heap_buffer_mismatch(self):
        heap0 = Heap(HEAP_DEFAULT, 1024, device=self.device0)
        self.assertRaises(
            BufferException, Buffer, 1024, device=self.device1, heap=heap0
        )

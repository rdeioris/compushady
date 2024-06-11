import unittest
from compushady import Texture2D, Buffer, HEAP_UPLOAD, HEAP_READBACK, Heap, HEAP_DEFAULT
from compushady.formats import (
    R8G8B8A8_UINT,
    R8G8B8A8_UNORM,
    get_pixel_size,
    R16G16B16A16_FLOAT,
)
import compushady.config

compushady.config.set_debug(True)
import struct


class Texture2DTests(unittest.TestCase):

    def test_simple_upload(self):
        t0 = Texture2D(2, 2, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size)
        b2 = Buffer(t0.size, HEAP_READBACK)
        for y in range(0, t0.height):
            b0.upload(b"\xDE\xAD\xBE\xEF" * 2, offset=t0.row_pitch * y)
        b0.copy_to(t0)
        t0.copy_to(b1)
        b1.copy_to(b2)
        self.assertEqual(b2.readback(4), b"\xDE\xAD\xBE\xEF")

    def test_simple_upload_float(self):
        t0 = Texture2D(2, 2, R16G16B16A16_FLOAT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size, HEAP_READBACK)
        b0.upload(struct.pack("4f", 1, 2, 3, 4))
        b0.copy_to(t0)
        t0.copy_to(b1)
        self.assertEqual(b1.readback(16), struct.pack("4f", 1, 2, 3, 4))

    def test_simple_upload2d(self):
        t0 = Texture2D(2, 2, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b1 = Buffer(t0.size, HEAP_READBACK)
        b2 = Buffer(t0.size)
        b0.upload2d(
            b"\xDE\xAD\xBE\xEF",
            t0.row_pitch,
            t0.width,
            t0.height,
            get_pixel_size(R8G8B8A8_UINT),
        )
        b0.copy_to(t0)
        t0.copy_to(b2)
        b2.copy_to(b1)
        self.assertEqual(b1.readback(4), b"\xDE\xAD\xBE\xEF")

    def test_simple_copy(self):
        t0 = Texture2D(4, 4, R8G8B8A8_UINT)
        b0 = Buffer(t0.size, HEAP_UPLOAD)
        b0.upload(b"\1\2\3\4")
        b0.copy_to(t0)
        t1 = Texture2D(4, 4, R8G8B8A8_UNORM)
        t0.copy_to(t1)
        b1 = Buffer(t0.size)
        b2 = Buffer(t0.size, HEAP_READBACK)
        t1.copy_to(b1)
        b1.copy_to(b2)
        self.assertEqual(b2.readback(4), b"\1\2\3\4")

    def test_upload2d_readback2d(self):
        t0 = Texture2D(2, 2, R8G8B8A8_UNORM)
        t0_upload = Buffer(t0.size, HEAP_UPLOAD)
        t0_upload.upload2d(
            b"\xff\xff\xff\xff\xaa\xaa\xaa\xaa\xbb\xbb\xbb\xbb\xcc\xcc\xcc\xcc",
            t0.row_pitch,
            t0.width,
            t0.height,
            4,
        )
        t0_upload.copy_to(t0)
        t0_readback = Buffer(t0.size, HEAP_READBACK)
        t0.copy_to(t0_readback)
        self.assertEqual(
            t0_readback.readback2d(t0.row_pitch, t0.width, t0.height, 4),
            b"\xff\xff\xff\xff\xaa\xaa\xaa\xaa\xbb\xbb\xbb\xbb\xcc\xcc\xcc\xcc",
        )

    def test_sparse(self):
        heap = Heap(HEAP_DEFAULT, 1024 * 1024)
        t0 = Texture2D(1024, 1024, format=R8G8B8A8_UNORM, sparse=True)

        staging_texture = Texture2D(
            t0.tile_width, t0.tile_height, format=R8G8B8A8_UNORM
        )

        b_upload = Buffer(staging_texture.size, HEAP_UPLOAD)
        b_readback = Buffer(staging_texture.size, HEAP_READBACK)

        b_upload.upload(b"\xff\xee\xdd\xaa")

        t0.bind_tile(0, 0, 0, heap)
        t0.bind_tile(1, 0, 0, heap)
        t0.bind_tile(2, 0, 0, heap)

        b_upload.copy_to(staging_texture)
        staging_texture.copy_to(
            t0, dst_x=0, dst_y=0, width=t0.tile_width, height=t0.tile_height
        )

        t0.copy_to(
            staging_texture,
            src_x=0,
            src_y=0,
            width=t0.tile_width,
            height=t0.tile_height,
        )
        staging_texture.copy_to(b_readback)
        self.assertEqual(b_readback.readback(4), b"\xff\xee\xdd\xaa")

        t0.copy_to(
            staging_texture,
            src_x=t0.tile_width,
            src_y=0,
            width=t0.tile_width,
            height=t0.tile_height,
        )
        staging_texture.copy_to(b_readback)
        self.assertEqual(b_readback.readback(4), b"\xff\xee\xdd\xaa")

        t0.copy_to(
            staging_texture,
            src_x=t0.tile_width * 2,
            src_y=0,
            width=t0.tile_width,
            height=t0.tile_height,
        )
        staging_texture.copy_to(b_readback)
        self.assertEqual(b_readback.readback(4), b"\xff\xee\xdd\xaa")

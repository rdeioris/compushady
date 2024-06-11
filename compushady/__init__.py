import importlib
from . import config
import atexit
import os

HEAP_DEFAULT = 0
HEAP_UPLOAD = 1
HEAP_READBACK = 2

SHADER_BINARY_TYPE_DXIL = 0
SHADER_BINARY_TYPE_SPIRV = 1
SHADER_BINARY_TYPE_DXBC = 2
SHADER_BINARY_TYPE_MSL = 3
SHADER_BINARY_TYPE_GLSL = 4

SAMPLER_FILTER_POINT = 0
SAMPLER_FILTER_LINEAR = 1

SAMPLER_ADDRESS_MODE_WRAP = 0
SAMPLER_ADDRESS_MODE_MIRROR = 1
SAMPLER_ADDRESS_MODE_CLAMP = 2


class UnknownBackend(Exception):
    pass


class BufferException(Exception):
    pass


class Texture1DException(Exception):
    pass


class Texture2DException(Exception):
    pass


class Texture3DException(Exception):
    pass


class SamplerException(Exception):
    pass


class HeapException(Exception):
    pass


_backend = None
_discovered_devices = None
_current_device = None


def get_backend():
    def debug_callback():
        messages = get_current_device().get_debug_messages()
        for message in messages:
            print(message)

    global _backend
    if _backend is None:
        _backend = importlib.import_module(
            "compushady.backends.{0}".format(config.wanted_backend)
        )
        if config.debug:
            _backend.enable_debug()
            atexit.register(debug_callback)
    return _backend


def get_discovered_devices():
    global _discovered_devices
    if _discovered_devices is None:
        _discovered_devices = get_backend().get_discovered_devices()
    return _discovered_devices


def set_current_device(index):
    global _current_device
    _current_device = get_discovered_devices()[index]


def get_current_device():
    global _current_device
    if _current_device is None:
        _current_device = get_best_device()
    return _current_device


def get_best_device():
    if "COMPUSHADY_DEVICE" in os.environ:
        return get_discovered_devices()[int(os.environ["COMPUSHADY_DEVICE"])]
    return sorted(
        get_discovered_devices(),
        key=lambda x: (x.is_hardware, x.is_discrete, x.dedicated_video_memory),
    )[-1]


class Resource:
    def copy_to(
        self,
        destination,
        size=0,
        src_offset=0,
        dst_offset=0,
        width=0,
        height=0,
        depth=0,
        src_x=0,
        src_y=0,
        src_z=0,
        dst_x=0,
        dst_y=0,
        dst_z=0,
        src_slice=0,
        dst_slice=0,
    ):
        self.handle.copy_to(
            destination.handle,
            size,
            src_offset,
            dst_offset,
            width,
            height,
            depth,
            src_x,
            src_y,
            src_z,
            dst_x,
            dst_y,
            dst_z,
            src_slice,
            dst_slice,
        )

    @property
    def size(self):
        return self.handle.size

    @property
    def heap_size(self):
        return self.handle.heap_size

    def bind_tile(
        self,
        x,
        y,
        z,
        heap,
        heap_offset=0,
    ):
        self.handle.bind_tile(x, y, z, heap.handle if heap else None, heap_offset)

    @property
    def tiles_x(self):
        return self.handle.tiles_x

    @property
    def tiles_y(self):
        return self.handle.tiles_y

    @property
    def tiles_z(self):
        return self.handle.tiles_z

    @property
    def tile_width(self):
        return self.handle.tile_width

    @property
    def tile_height(self):
        return self.handle.tile_height

    @property
    def tile_depth(self):
        return self.handle.tile_depth


class Buffer(Resource):
    def __init__(
        self,
        size,
        heap_type=HEAP_DEFAULT,
        stride=0,
        format=0,
        heap=None,
        heap_offset=0,
        sparse=False,
        device=None,
    ):
        self.device = device if device else get_current_device()
        self.heap = heap
        self.handle = self.device.create_buffer(
            heap_type,
            size,
            stride,
            format,
            heap.handle if heap else None,
            heap_offset,
            sparse,
        )

    def upload(self, data, offset=0):
        self.handle.upload(data, offset)

    def upload2d(self, data, pitch, width, height, bytes_per_pixel):
        return self.handle.upload2d(data, pitch, width, height, bytes_per_pixel)

    def upload_chunked(self, data, stride, filler):
        return self.handle.upload_chunked(data, stride, filler)

    def readback(self, buffer_or_size=0, offset=0):
        if isinstance(buffer_or_size, int):
            return self.handle.readback(buffer_or_size, offset)
        self.handle.readback_to_buffer(buffer_or_size, offset)

    def readback2d(self, pitch, width, height, bytes_per_pixel):
        return self.handle.readback2d(pitch, width, height, bytes_per_pixel)


class Texture1D(Resource):
    def __init__(self, width, format, heap=None, heap_offset=0, slices=1, device=None):
        self.device = device if device else get_current_device()
        self.heap = heap
        self.handle = self.device.create_texture1d(
            width, format, heap.handle if heap else None, heap_offset, slices
        )

    @property
    def width(self):
        return self.handle.width

    @property
    def slices(self):
        return self.handle.slices

    @property
    def row_pitch(self):
        return self.handle.row_pitch


class Texture2D(Resource):
    def __init__(
        self, width, height, format, heap=None, heap_offset=0, slices=1, device=None
    ):
        self.device = device if device else get_current_device()
        self.heap = heap
        self.handle = self.device.create_texture2d(
            width, height, format, heap.handle if heap else None, heap_offset, slices
        )

    @property
    def width(self):
        return self.handle.width

    @property
    def height(self):
        return self.handle.height

    @property
    def slices(self):
        return self.handle.slices

    @property
    def row_pitch(self):
        return self.handle.row_pitch


class Texture3D(Resource):
    def __init__(
        self, width, height, depth, format, heap=None, heap_offset=0, device=None
    ):
        self.device = device if device else get_current_device()
        self.heap = heap
        self.handle = self.device.create_texture3d(
            width, height, depth, format, heap.handle if heap else None, heap_offset
        )

    @property
    def width(self):
        return self.handle.width

    @property
    def height(self):
        return self.handle.height

    @property
    def depth(self):
        return self.handle.depth

    @property
    def row_pitch(self):
        return self.handle.row_pitch


class Swapchain:
    def __init__(
        self, window_handle, format, num_buffers=3, device=None, width=0, height=0
    ):
        self.device = device if device else get_current_device()
        self.handle = self.device.create_swapchain(
            window_handle, format, num_buffers, width, height
        )

    @property
    def width(self):
        return self.handle.width

    @property
    def height(self):
        return self.handle.height

    def present(self, resource, x=0, y=0):
        self.handle.present(resource.handle, x, y)


class Sampler:
    def __init__(
        self,
        address_mode_u=SAMPLER_ADDRESS_MODE_WRAP,
        address_mode_v=SAMPLER_ADDRESS_MODE_WRAP,
        address_mode_w=SAMPLER_ADDRESS_MODE_WRAP,
        filter_min=SAMPLER_FILTER_POINT,
        filter_mag=SAMPLER_FILTER_POINT,
        device=None,
    ):
        self.device = device if device else get_current_device()
        self.handle = self.device.create_sampler(
            address_mode_u, address_mode_v, address_mode_w, filter_min, filter_mag
        )


class Heap:
    def __init__(self, heap_type, size, device=None):
        self.device = device if device else get_current_device()
        self.handle = self.device.create_heap(heap_type, size)

    @property
    def size(self):
        return self.handle.size

    @property
    def heap_type(self):
        return self.handle.heap_type


class Compute:
    def __init__(
        self,
        shader,
        cbv=[],
        srv=[],
        uav=[],
        samplers=[],
        push_size=0,
        bindless=False,
        max_bindless=64,
        device=None,
    ):
        self.device = device if device else get_current_device()
        self.handle = self.device.create_compute(
            shader,
            cbv=[resource.handle for resource in cbv],
            srv=[resource.handle for resource in srv],
            uav=[resource.handle for resource in uav],
            samplers=[sampler.handle for sampler in samplers],
            push_size=push_size,
            bindless=max_bindless if bindless else 0,
        )

    def dispatch(self, x, y, z, push=None):
        self.handle.dispatch(x, y, z, push if push else b"")

    def dispatch_indirect(self, indirect_buffer, offset=0, push=None):
        self.handle.dispatch_indirect(
            indirect_buffer.handle, offset, push if push else b""
        )

    def bind_cbv(self, index, cbv):
        self.handle.bind_cbv(index, cbv.handle)

    def bind_srv(self, index, srv):
        self.handle.bind_srv(index, srv.handle)

    def bind_uav(self, index, uav):
        self.handle.bind_uav(index, uav.handle)

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
            'compushady.backends.{0}'.format(config.wanted_backend))
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
    if 'COMPUSHADY_DEVICE' in os.environ:
        return get_discovered_devices()[int(os.environ['COMPUSHADY_DEVICE'])]
    return sorted(get_discovered_devices(), key=lambda x: (
        x.is_hardware, x.is_discrete, x.dedicated_video_memory))[-1]


class Resource:
    def copy_to(self, destination):
        self.handle.copy_to(destination.handle)

    @property
    def size(self):
        return self.handle.size


class Buffer(Resource):
    def __init__(self, size, heap=HEAP_DEFAULT, stride=0, format=0, device=None):
        self.device = device if device else get_current_device()
        self.handle = self.device.create_buffer(heap, size, stride, format)

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

    def __init__(self, width, format, device=None):
        self.device = device if device else get_current_device()
        self.handle = self.device.create_texture1d(width, format)

    @classmethod
    def from_native(cls, ptr, device=None):
        instance = cls.__new__(cls)
        instance.device = device if device else get_current_device()
        instance.handle = instance.device.create_texture1d_from_native(ptr)
        return instance

    @property
    def width(self):
        return self.handle.width

    @property
    def row_pitch(self):
        return self.handle.row_pitch


class Texture2D(Resource):

    def __init__(self, width, height, format, device=None):
        self.device = device if device else get_current_device()
        self.handle = self.device.create_texture2d(width, height, format)

    @classmethod
    def from_native(cls, ptr, width, height, format, device=None):
        instance = cls.__new__(cls)
        instance.device = device if device else get_current_device()
        instance.handle = instance.device.create_texture2d_from_native(
            ptr, width, height, format)
        return instance

    @property
    def width(self):
        return self.handle.width

    @property
    def height(self):
        return self.handle.height

    @property
    def row_pitch(self):
        return self.handle.row_pitch


class Texture3D(Resource):

    def __init__(self, width, height, depth, format, device=None):
        self.device = device if device else get_current_device()
        self.handle = self.device.create_texture3d(
            width, height, depth, format)

    @classmethod
    def from_native(cls, ptr, device=None):
        instance = cls.__new__(cls)
        instance.device = device if device else get_current_device()
        instance.handle = instance.device.create_texture3d_from_native(ptr)
        return instance

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

    def __init__(self, window_handle, format, num_buffers=3, device=None):
        self.device = device if device else get_current_device()
        self.handle = self.device.create_swapchain(
            window_handle, format, num_buffers)

    @property
    def width(self):
        return self.handle.width

    @property
    def height(self):
        return self.handle.height

    def present(self, resource, x=0, y=0):
        self.handle.present(resource.handle, x, y)


class Compute:

    def __init__(self, shader, cbv=[], srv=[], uav=[], device=None):
        self.device = device if device else get_current_device()
        self.handle = self.device.create_compute(
            shader,
            cbv=[resource.handle for resource in cbv],
            srv=[resource.handle for resource in srv],
            uav=[resource.handle for resource in uav])

    def dispatch(self, x, y, z):
        self.handle.dispatch(x, y, z)

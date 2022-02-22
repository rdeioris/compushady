import importlib
from . import config

HEAP_DEFAULT = 0
HEAP_UPLOAD = 1
HEAP_READBACK = 2

SHADER_BINARY_TYPE_DXIL = 0
SHADER_BINARY_TYPE_SPIRV = 1


class UnknownBackend(Exception):
    pass


_backend = None
_discovered_devices = None
_current_device = None


def get_backend():
    global _backend
    if _backend is None:
        _backend = importlib.import_module(
            'compushady.backends.{0}'.format(config.wanted_backend))
    if config.debug:
        _backend.enable_debug()
    return _backend


def get_discovered_devices():
    global _discovered_devices
    if _discovered_devices is None:
        _discovered_devices = get_backend().get_discovered_devices()
    return _discovered_devices


def set_current_device(index):
    global _current_device
    _current_device = get_discovered_devices()[index]


def get_best_device():
    global _current_device
    if _current_device is not None:
        return _current_device
    _current_device = sorted(sorted(get_discovered_devices(), key=lambda x: 0 if x.is_discrete else 1), key=lambda x: x.dedicated_video_memory)[-1]
    return _current_device


class Resource:
    def copy_to(self, destination):
        self.handle.copy_to(destination.handle)

    @property
    def size(self):
        return self.handle.size


class Buffer(Resource):
    def __init__(self, size, heap=HEAP_DEFAULT, stride=0, format=0, device=None):
        self.device = device if device else get_best_device()
        self.handle = self.device.create_buffer(heap, size, stride, format)

    def upload(self, data, offset=0):
        self.handle.upload(data, offset)

    def upload2d(self, data, pitch, width, height, bytes_per_pixel):
        return self.handle.upload2d(data, pitch, width, height, bytes_per_pixel)

    def readback(self, buffer_or_size=0, offset=0):
        if isinstance(buffer_or_size, int):
            return self.handle.readback(buffer_or_size, offset)
        self.handle.readback_to_buffer(buffer_or_size, offset)

    def readback2d(self, pitch, width, height, bytes_per_pixel):
        return self.handle.readback2d(pitch, width, height, bytes_per_pixel)


class Texture1D(Resource):

    def __init__(self, width, format, device=None):
        self.device = device if device else get_best_device()
        self.handle = self.device.create_texture1d(width, format)

    @classmethod
    def from_native(cls, ptr, device=None):
        instance = cls.__new__(cls)
        instance.device = device if device else get_best_device()
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
        self.device = device if device else get_best_device()
        self.handle = self.device.create_texture2d(width, height, format)

    @classmethod
    def from_native(cls, ptr, device=None):
        instance = cls.__new__(cls)
        instance.device = device if device else get_best_device()
        instance.handle = instance.device.create_texture2d_from_native(ptr)
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


class Swapchain:

    def __init__(self, window_handle, format, num_buffers=2, device=None):
        self.device = device if device else get_best_device()
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
        self.device = device if device else get_best_device()
        self.handle = self.device.create_compute(
            shader,
            cbv=[resource.handle for resource in cbv],
            srv=[resource.handle for resource in srv],
            uav=[resource.handle for resource in uav])

    def dispatch(self, x, y, z):
        self.handle.dispatch(x, y, z)

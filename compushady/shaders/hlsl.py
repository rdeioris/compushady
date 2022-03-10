from compushady.backends import dxc
from compushady import get_backend
import os
import platform

lib_dir = os.path.join(os.path.dirname(__file__), '..', 'backends')

if platform.system() == 'Windows':
    if hasattr(os, 'add_dll_directory'):
        os.add_dll_directory(lib_dir)
    else:
        import ctypes
        ctypes.windll.kernel32.AddDllDirectory(lib_dir)
elif platform.system() == 'Linux':
    lib_path = os.path.join(lib_dir, 'libdxcompiler.so.3.7')
    import ctypes
    ctypes.CDLL(lib_path, ctypes.RTLD_GLOBAL)
elif platform.system() == 'Darwin':
    lib_path = os.path.join(lib_dir, 'libdxcompiler.3.7.dylib')
    import ctypes
    ctypes.CDLL(lib_path, ctypes.RTLD_GLOBAL)


def compile(source, entry_point='main'):
    return dxc.compile(source, entry_point, get_backend().get_shader_binary_type())

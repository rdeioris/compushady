from compushady.backends import dxc
from compushady import get_backend, SHADER_BINARY_TYPE_MSL
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
    if platform.machine() == 'armv7l':
        lib_path = os.path.join(lib_dir, 'libdxcompiler_armv7l.so.3.7')
    elif platform.machine() == 'aarch64':
        lib_path = os.path.join(lib_dir, 'libdxcompiler_aarch64.so.3.7')
    else:
        lib_path = os.path.join(lib_dir, 'libdxcompiler_x86_64.so.3.7')
    import ctypes
    ctypes.CDLL(lib_path, ctypes.RTLD_GLOBAL)
elif platform.system() == 'Darwin':
    lib_path = os.path.join(lib_dir, 'libdxcompiler.3.7.dylib')
    import ctypes
    ctypes.CDLL(lib_path, ctypes.RTLD_GLOBAL)


def compile(source, entry_point='main'):
    blob = dxc.compile(source, entry_point, get_backend().get_shader_binary_type())
    if get_backend().get_shader_binary_type() == SHADER_BINARY_TYPE_MSL:
        from compushady.backends import metal
        if entry_point == 'main':
            entry_point = 'main0'
        return metal.msl_compile(blob[0], entry_point, blob[1])
    return blob

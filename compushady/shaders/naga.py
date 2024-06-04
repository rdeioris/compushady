import os
import platform
import ctypes

lib_dir = os.path.join(os.path.dirname(__file__), "..", "backends")

if platform.system() == "Windows":
    lib_name = "compushady_naga.dll"
elif platform.system() == "Linux":
    if platform.machine() == "aarch64":
        import sys
        if sys.maxsize > 2 ** 32:
            lib_name = "libcompushady_naga_aarch64.so"
        else:
            lib_name = "libcompushady_naga_armhf.so"
    elif platform.machine() == "armv7l":
        lib_name = "libcompushady_naga_armhf.so"
    else:
        lib_name = "libcompushady_naga_x86_64.so"
elif platform.system() == "Darwin":
    lib_name = "libcompushady_naga.dylib"

naga = ctypes.CDLL(os.path.join(lib_dir, lib_name), ctypes.RTLD_GLOBAL)
naga.compushady_naga_wgsl_to_hlsl.restype = ctypes.POINTER(ctypes.c_ubyte)
naga.compushady_naga_wgsl_to_spv.restype = ctypes.POINTER(ctypes.c_ubyte)
naga.compushady_naga_wgsl_to_msl.restype = ctypes.POINTER(ctypes.c_ubyte)
naga.compushady_naga_glsl_to_hlsl.restype = ctypes.POINTER(ctypes.c_ubyte)
naga.compushady_naga_glsl_to_spv.restype = ctypes.POINTER(ctypes.c_ubyte)
naga.compushady_naga_glsl_to_msl.restype = ctypes.POINTER(ctypes.c_ubyte)
naga.compushady_naga_free.argtypes = [ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t]

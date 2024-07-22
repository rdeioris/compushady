import os
import platform
import ctypes

lib_dir = os.path.join(os.path.dirname(__file__), "..", "backends")

if platform.system() == "Windows":
    lib_name = "compushady_khr.dll"
elif platform.system() == "Linux":
    if platform.machine() == "aarch64":
        import sys

        if sys.maxsize > 2**32:
            lib_name = "libcompushady_khr_aarch64.so"
        else:
            lib_name = "libcompushady_khr_armhf.so"
    elif platform.machine() == "armv7l":
        lib_name = "libcompushady_khr_armhf.so"
    else:
        lib_name = "libcompushady_khr_x86_64.so"
elif platform.system() == "Darwin":
    lib_name = "libcompushady_khr.dylib"

khr = ctypes.CDLL(os.path.join(lib_dir, lib_name), ctypes.RTLD_GLOBAL)
khr.compushady_khr_malloc.restype = ctypes.c_void_p
khr.compushady_khr_free.restype = None
khr.compushady_khr_glsl_to_spv.restype = ctypes.POINTER(ctypes.c_ubyte)


def glsl_to_spv(source, entry_point="main", target="cs_6_0"):
    source_buffer = source.encode()
    spv_size = ctypes.c_size_t()
    error_ptr = ctypes.c_char_p()
    error_len = ctypes.c_size_t()
    spv = khr.compushady_khr_glsl_to_spv(
        source_buffer,
        len(source_buffer),
        target,
        1,
        ctypes.pointer(spv_size),
        ctypes.pointer(error_ptr),
        ctypes.pointer(error_len),
        khr.compushady_khr_malloc,
    )

    if not spv:
        error = ctypes.string_at(error_ptr, error_len.value).decode()
        khr.compushady_khr_free(error_ptr)
        raise ValueError(error)

    spirv = ctypes.string_at(spv, spv_size.value)
    khr.compushady_khr_free(spv)
    return spirv

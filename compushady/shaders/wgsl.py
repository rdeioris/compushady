from compushady import (
    get_backend,
    SHADER_BINARY_TYPE_MSL,
    SHADER_BINARY_TYPE_DXIL,
    SHADER_BINARY_TYPE_SPIRV,
)
import os
import platform
import ctypes

lib_dir = os.path.join(os.path.dirname(__file__), "..", "backends")

if platform.system() == "Windows":
    lib_name = "compushady_naga.dll"

naga = ctypes.CDLL(os.path.join(lib_dir, lib_name), ctypes.RTLD_GLOBAL)
naga.compushady_naga_wgsl_to_hlsl.restype = ctypes.POINTER(ctypes.c_ubyte)
naga.compushady_naga_wgsl_to_spv.restype = ctypes.POINTER(ctypes.c_ubyte)
naga.compushady_naga_free.argtypes = [ctypes.POINTER(ctypes.c_ubyte), ctypes.c_size_t]


def compile(source, entry_point="main", target="cs_6_0"):
    output_len = ctypes.c_size_t()
    error_len = ctypes.c_size_t()
    error_ptr = ctypes.c_char_p()

    source_blob = source.encode("utf8")

    if get_backend().get_shader_binary_type() == SHADER_BINARY_TYPE_DXIL:
        hlsl_ptr = naga.compushady_naga_wgsl_to_hlsl(
            source_blob,
            len(source_blob),
            ctypes.pointer(output_len),
            ctypes.pointer(error_ptr),
            ctypes.pointer(error_len),
        )

        if not hlsl_ptr:
            error = error_ptr.value[0 : error_len.value].decode()
            naga.compushady_naga_free(
                ctypes.cast(error_ptr, ctypes.POINTER(ctypes.c_ubyte)), output_len.value
            )
            raise ValueError(error)

        from compushady.shaders import hlsl

        hlsl_source = bytes(hlsl_ptr[0 : output_len.value]).decode()
        naga.compushady_naga_free(hlsl_ptr, output_len.value)
        return hlsl.compile(hlsl_source, entry_point, target)
    elif get_backend().get_shader_binary_type() == SHADER_BINARY_TYPE_SPIRV:
        spv_ptr = naga.compushady_naga_wgsl_to_spv(
            source_blob,
            len(source_blob),
            ctypes.pointer(output_len),
            ctypes.pointer(error_ptr),
            ctypes.pointer(error_len),
        )

        if not spv_ptr:
            error = error_ptr.value[0 : error_len.value].decode()
            naga.compushady_naga_free(
                ctypes.cast(error_ptr, ctypes.POINTER(ctypes.c_ubyte)), output_len.value
            )
            raise ValueError(error)

        spv = bytes(spv_ptr[0 : output_len.value])
        naga.compushady_naga_free(spv_ptr, output_len.value)
        return spv

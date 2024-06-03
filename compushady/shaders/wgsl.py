from compushady import (
    get_backend,
    SHADER_BINARY_TYPE_MSL,
    SHADER_BINARY_TYPE_DXIL,
    SHADER_BINARY_TYPE_SPIRV,
)
from compushady.shaders.naga import naga
import ctypes


def compile(source, entry_point="main", target="cs_6_0"):
    output_len = ctypes.c_size_t()
    error_len = ctypes.c_size_t()
    error_ptr = ctypes.c_char_p()

    source_blob = source.encode("utf8")
    target_blob = target.encode("utf8")
    entry_point_blob = entry_point.encode("utf8")

    if get_backend().get_shader_binary_type() == SHADER_BINARY_TYPE_DXIL:
        hlsl_ptr = naga.compushady_naga_wgsl_to_hlsl(
            source_blob,
            len(source_blob),
            target_blob,
            len(source_blob),
            ctypes.c_uint32(1),
            ctypes.pointer(output_len),
            ctypes.pointer(error_ptr),
            ctypes.pointer(error_len),
        )

        if not hlsl_ptr:
            error = error_ptr.value[0 : error_len.value].decode("utf8")
            naga.compushady_naga_free(
                ctypes.cast(error_ptr, ctypes.POINTER(ctypes.c_ubyte)), output_len.value
            )
            raise ValueError(error)

        from compushady.shaders import hlsl

        hlsl_source = bytes(hlsl_ptr[0 : output_len.value]).decode("utf8")
        naga.compushady_naga_free(hlsl_ptr, output_len.value)
        return hlsl.compile(hlsl_source, entry_point, target)

    elif get_backend().get_shader_binary_type() == SHADER_BINARY_TYPE_SPIRV:
        spv_ptr = naga.compushady_naga_wgsl_to_spv(
            source_blob,
            len(source_blob),
            entry_point_blob,
            len(entry_point_blob),
            ctypes.c_uint32(1),
            ctypes.pointer(output_len),
            ctypes.pointer(error_ptr),
            ctypes.pointer(error_len),
        )

        if not spv_ptr:
            error = error_ptr.value[0 : error_len.value].decode("utf8")
            naga.compushady_naga_free(
                ctypes.cast(error_ptr, ctypes.POINTER(ctypes.c_ubyte)), output_len.value
            )
            raise ValueError(error)

        spv = bytes(spv_ptr[0 : output_len.value])
        naga.compushady_naga_free(spv_ptr, output_len.value)
        return spv

    elif get_backend().get_shader_binary_type() == SHADER_BINARY_TYPE_MSL:
        x = ctypes.c_uint32()
        y = ctypes.c_uint32()
        z = ctypes.c_uint32()
        msl_ptr = naga.compushady_naga_wgsl_to_msl(
            source_blob,
            len(source_blob),
            entry_point_blob,
            len(entry_point_blob),
            ctypes.c_uint32(1),
            ctypes.pointer(output_len),
            ctypes.pointer(error_ptr),
            ctypes.pointer(error_len),
            ctypes.pointer(x),
            ctypes.pointer(y),
            ctypes.pointer(z),
        )

        if not msl_ptr:
            error = error_ptr.value[0 : error_len.value].decode("utf8")
            naga.compushady_naga_free(
                ctypes.cast(error_ptr, ctypes.POINTER(ctypes.c_ubyte)), output_len.value
            )
            raise ValueError(error)

        msl_source = bytes(msl_ptr[0 : output_len.value]).decode("utf8")
        naga.compushady_naga_free(msl_ptr, output_len.value)

        from compushady.backends import metal

        return metal.msl_compile(msl_source, entry_point, (x.value, y.value, z.value))

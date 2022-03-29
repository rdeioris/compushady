from setuptools import setup, Extension
import os
import platform

is_windows = platform.system() == 'Windows'
is_mac = platform.system() == 'Darwin'

build_vulkan = not is_windows and not is_mac

vulkan_include_dirs = []
vulkan_library_dirs = []

if is_windows:
    if 'VULKAN_SDK' in os.environ:
        vulkan_include_dirs = [os.path.join(
            os.environ['VULKAN_SDK'], 'Include')]
        vulkan_library_dirs = [os.path.join(os.environ['VULKAN_SDK'], 'Lib')]
        build_vulkan = True

if is_mac:
    vulkan_base = '/usr/local'
    if 'VULKAN_SDK' in os.environ:
        vulkan_base = os.environ['VULKAN_SDK']
    vulkan_hpp = os.path.join(vulkan_base, 'include', 'vulkan', 'vulkan.hpp')
    if os.path.exists(vulkan_hpp):
        vulkan_include_dirs = [os.path.join(vulkan_base, 'include')]
        vulkan_library_dirs = [os.path.join(vulkan_base, 'lib')]
        build_vulkan = True

backends = []

if build_vulkan:
    backends.append(Extension('compushady.backends.vulkan',
                              libraries=[
                                  'vulkan-1' if is_windows else 'vulkan'],
                              include_dirs=vulkan_include_dirs,
                              library_dirs=vulkan_library_dirs,
                              depends=['compushady/backends/compushady.h'],
                              sources=['compushady/backends/vulkan.cpp',
                                       'compushady/backends/common.cpp'],
                              extra_compile_args=[
                                  '-std=c++14'] if not is_windows else [],
                              extra_link_args=[
                                  '-Wl,-rpath,{0}'.format(vulkan_library_dirs[0])] if is_mac else [],
                              ))


if is_windows:
    backends.append(Extension('compushady.backends.d3d12',
                              libraries=['dxgi', 'd3d12'],
                              depends=['compushady/backends/compushady.h'],
                              sources=['compushady/backends/d3d12.cpp',
                                       'compushady/backends/dxgi.cpp',
                                       'compushady/backends/common.cpp', ]
                              ))
    backends.append(Extension('compushady.backends.d3d11',
                              libraries=['dxgi', 'd3d11'],
                              depends=['compushady/backends/compushady.h'],
                              sources=['compushady/backends/d3d11.cpp',
                                       'compushady/backends/dxgi.cpp',
                                       'compushady/backends/common.cpp']
                              ))

if is_mac:
    backends.append(Extension('compushady.backends.metal',
                              depends=['compushady/backends/compushady.h'],
                              sources=['compushady/backends/metal.m',
                                       'compushady/backends/common.cpp'],
                              extra_compile_args=[
                                  '-ObjC++', '-Wno-unguarded-availability-new'],
                              extra_link_args=['-Wl,-framework,MetalKit'],
                              ))

spirv_cross_sources = [
    'compushady/backends/spirv_cross/{0}'.format(source) for source in ['spirv_cross.cpp',
                                                                        'spirv_cfg.cpp',
                                                                        'spirv_cross_parsed_ir.cpp',
                                                                        'spirv_parser.cpp',
                                                                        'spirv_glsl.cpp']]

backends.append(Extension('compushady.backends.dxc',
                          libraries=['d3dcompiler'] if is_windows else [],
                          depends=['compushady/backends/compushady.h'],
                          sources=['compushady/backends/dxc.cpp',
                                   'compushady/backends/common.cpp',
                                   'compushady/backends/spirv_cross/spirv_msl.cpp'] + spirv_cross_sources,
                          extra_compile_args=[
                              '-std=c++14'] if not is_windows else [],
                          ))

additional_files = []
if is_windows:
    additional_files = ['backends/dxcompiler.dll', 'backends/dxil.dll']
elif platform.system() == 'Linux':
    if platform.machine() == 'armv7l':
        additional_files = ['backends/libdxcompiler_armv7l.so.3.7']
    elif platform.machine() == 'aarch64':
        additional_files = ['backends/libdxcompiler_aarch64.so.3.7']
    else:
        additional_files = ['backends/libdxcompiler_x86_64.so.3.7']
elif is_mac:
    additional_files = ['backends/libdxcompiler.3.7.dylib']

setup(name='compushady',
      version='0.17.1',
      description='The compushady GPU Compute module',
      author='Roberto De Ioris',
      author_email='roberto.deioris@gmail.com',
      url='https://github.com/rdeioris/compushady',
      long_description='''
Run GPU Compute Shaders from Python.
''',
      setup_requires=['wheel'],
      packages=['compushady', 'compushady.shaders'],
      package_data={'compushady': additional_files},
      ext_modules=backends,
      )

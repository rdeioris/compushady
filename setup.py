from setuptools import setup, Extension
import platform

backends = [Extension('compushady.backends.vulkan',
                      libraries=['vulkan-1' if platform.system() ==
                                 'Windows' else 'vulkan'],
                      sources=['compushady/backends/vulkan.cpp']
                      )]


if platform.system() == 'Windows':
    backends.append(Extension('compushady.backends.d3d12',
                              libraries=['dxgi', 'd3d12'],
                              sources=['compushady/backends/d3d12.cpp']
                              ))
    backends.append(Extension('compushady.backends.d3d11',
                              libraries=['dxgi', 'd3d11'],
                              sources=['compushady/backends/d3d11.cpp']
                              ))

backends.append(Extension('compushady.backends.dxc',
                          sources=['compushady/backends/dxc.cpp']
                          ))

additional_files = []
if platform.system() == 'Windows':
    additional_files = ['backends/dxcompiler.dll', 'backends/dxil.dll']
elif platform.system() == 'Linux':
    additional_files = ['backends/libdxcompiler.so.3.7']

setup(name='compushady',
      version='0.1',
      description='The compushady GPU Compute module',
      author='Roberto De Ioris',
      author_email='roberto.deioris@gmail.com',
      url='https://github.com/rdeioris/compushady',
      long_description='''
Run GPU Compute kernels from Python.
''',
      setup_requires=['wheel'],
      packages=['compushady', 'compushady.shaders'],
      package_data={'compushady': additional_files},
      ext_modules=backends,
      )

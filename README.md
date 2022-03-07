# compushady
Python module for easily running Compute Shaders

Currently d3d12 (Windows), vulkan (Linux x86-64 and Windows) and d3d11 (Windows) are supported. 

You can currently write shaders in HLSL, they will be compiled in the appropriate format (DXIL, DXBC, SPIR-V, ...) automatically.

OpenGL, Metal and GLSL backends are expected to be released in the future.

## Quickstart

```sh
pip install compushady
```

### Enumerate compute devices

```py
import compushady

for device in compushady.get_discovered_devices():
    name = device.name
    video_memory_in_mb = device.dedicated_video_memory // 1024 // 1024
    print('Name: {0} Dedicated Memory: {1} MB'.format(name, video_memory_in_mb))
```


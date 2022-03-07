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

### Upload data to a Texture

A Texture is an object available in the GPU memory. To upload data into it you need a so-called 'staging buffer'.
A staging buffer is a block of memory allocated in your system ram that is mappable from your GPU. Using it the GPU can copy data from that buffer to the texture memory.
To wrap up: if you want to upload data to the GPU, you first need to map a memory area in your ram (a buffer) and then ask the GPU to copy it in its memory (this assumes obviously a discrete GPU with on-board memory)

```py
from compushady import Buffer, Texture2D, HEAP_UPLOAD
from compushady.formats import R8G8B8A8_UINT

# creates a 8x8 texture in GPU with the classig RGBA 8 bit format
texture = Texture2D(8, 8, R8G8B8A8_UINT)  
# creates a staging buffer with the right size and in memory optimized for uploading data
staging_buffer = Buffer(texture.size, HEAP_UPLOAD)
# upload a bunch of pixels data into the staging_buffer
staging_buffer.upload(b'\xff\x00\x00\xff') # first pixel as red
# copy from the staging_buffer to the texture
staging_buffer.copy_to(texture)
```

### Reading back from GPU memory to system memory

Now that you have your data in GPU memory, you can manipulate them using a compute shader but, before seeing this, we need to learn how to copy back data from the texture memory to our system ram. We need a buffer again (this time a readback one):

```python
from compushady import HEAP_READBACK, Buffer, Texture2D, HEAP_UPLOAD
from compushady.formats import R8G8B8A8_UINT

# creates a 8x8 texture in GPU with the classig RGBA 8 bit format
texture = Texture2D(8, 8, R8G8B8A8_UINT)  
# creates a staging buffer with the right size and in memory optimized for uploading data
staging_buffer = Buffer(texture.size, HEAP_UPLOAD)
# upload a bunch of pixels data into the staging_buffer
staging_buffer.upload(b'\xff\x00\x00\xff') # first pixel as red
# copy from the staging_buffer to the texture
staging_buffer.copy_to(texture)

# do something with the texture...

# prepare the readback buffer
readback_buffer = Buffer(texture.size, HEAP_READBACK)
# copy from texture to the readback buffer
texture.copy_to(readback_buffer)

# get the data as a python bytes object (just the first 4 bytes)
print(readback_buffer.readback())
```

# Notes for building third party libraries

## DirectXCompiler universal library on Mac

```/Applications/CMake.app/Contents/bin/cmake .. -DCMAKE_BUILD_TYPE=Release -C ../cmake/caches/PredefinedParams.cmake -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"```

## Linux builds

Ubuntu 18 with gcc-8 has been choosen as the building system

Ensure to set gcc-8/g++-8 as the default build system:

```sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-8 800 --slave /usr/bin/g++ g++ /usr/bin/g++-8```

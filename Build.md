# Notes for building third party libraries

## DirectXCompiler universal library on Mac

```/Applications/CMake.app/Contents/bin/cmake .. -DCMAKE_BUILD_TYPE=Release -C ../cmake/caches/PredefinedParams.cmake -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"```

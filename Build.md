# Notes for building third party libraries

## DirectXCompiler universal library on Mac

```/Applications/CMake.app/Contents/bin/cmake .. -DCMAKE_BUILD_TYPE=Release -C ../cmake/caches/PredefinedParams.cmake -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"```

## compushady-naga universal library on Mac

```
cargo build --target=aarch64-apple-darwin --release
cargo build --target=x86_64-apple-darwin --release
lipo -create -output libcompushady_naga.dylib target/aarch64-apple-darwin/release/libcompushady_naga.dylib target/x86_64-apple-darwin/release/libcompushady_naga.dylib
```

## Linux builds

Ubuntu 18 with gcc-8 has been choosen as the building system

Ensure to set gcc-8/g++-8 as the default build system:

```sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-8 800 --slave /usr/bin/g++ g++ /usr/bin/g++-8```

## DirectXCompiler Android cross-build

```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -C ../cmake/caches/PredefinedParams.cmake  -DCMAKE_CXX_COMPILER=<ndk_toolchain>/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang++ -DCMAKE_C_COMPILER=<ndk_toolchain>/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang
make
```

Note: you will get errors about llvm-strip and clang-strip (they need to be executed on the building machine, but they get compiled for android), you need to have a build of them for your native platform and just substitute the android ones with them.


#! /bin/bash

cmake -H. -Bbuild -DCMAKE_C_COMPILER=${TOOLCHAIN}-gcc -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
cmake --build build
upx -9 build/ipcinfo
upx -9 build/ipctool

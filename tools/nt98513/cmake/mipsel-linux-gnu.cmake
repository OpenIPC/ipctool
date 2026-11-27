# Example for a compatible GCC already installed in PATH; no download/bootstrap.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mips)
set(CMAKE_C_COMPILER mipsel-linux-gnu-gcc)
set(CMAKE_ASM_COMPILER mipsel-linux-gnu-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

cmake_minimum_required(VERSION 2.8)
project(ipc_chip_info C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-std=gnu99 -static")

include_directories(
    ./src/
)

add_executable(ipc_chip_info
    src/main.c
    src/chipid.h
    src/chipid.c
    src/fake_symbols.c
    src/sensorid.c
    src/sensorid.c
    src/tools.h
    src/tools.c
)
target_link_libraries( ipc_chip_info dl )
install(TARGETS ipc_chip_info RUNTIME DESTINATION /usr/bin/)

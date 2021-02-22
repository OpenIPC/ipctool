cmake_minimum_required(VERSION 2.8)
project(ipc_chip_info C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-std=gnu99")

if(NOT BUILD_SHARED_LIBS)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_C_FLAGS "-static ${CMAKE_C_FLAGS}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s -Os -ffunction-sections
    -Wl,--gc-sections -DNDEBUG")
endif()

# Add a custom command that produces version.cpp, plus
# a dummy output that's not actually produced, in order
# to force version.cmake to always be re-run before the build
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/version.c
           ${CMAKE_CURRENT_BINARY_DIR}/_version.c
    COMMAND ${CMAKE_COMMAND} -P
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake)

include_directories(
    ./src/
)

add_executable(ipc_chip_info
    src/chipid.c
    src/chipid.c
    src/chipid.h
    src/fake_symbols.c
    src/hal_common.c
    src/hal_common.h
    src/hal_hisi.c
    src/hal_hisi.h
    src/hal_xm.c
    src/hal_xm.h
    src/main.c
    src/mtd.c
    src/mtd.h
    src/network.c
    src/network.h
    src/ram.c
    src/ram.h
    src/sensors.c
    src/sensors.c
    src/stack.c
    src/tools.c
    src/tools.h
    src/version.h

    src/vendors/xm.h
    src/vendors/xm.c

    ${CMAKE_CURRENT_BINARY_DIR}/version.c
)

target_link_libraries( ipc_chip_info dl )
install(TARGETS ipc_chip_info RUNTIME DESTINATION /usr/bin/)

cmake_minimum_required(VERSION 2.8)
project(ipc_chip_info C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-std=gnu99 -static")

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
    src/main.c
    src/chipid.h
    src/chipid.c
    src/fake_symbols.c
    src/sensorid.c
    src/sensorid.c
    src/tools.h
    src/tools.c
    src/version.h
    ${CMAKE_CURRENT_BINARY_DIR}/version.c
)

#find_package(Threads REQUIRED)
#target_link_libraries(ipc_chip_info Threads::Threads)

target_link_libraries( ipc_chip_info dl )
install(TARGETS ipc_chip_info RUNTIME DESTINATION /usr/bin/)

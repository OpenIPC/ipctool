cmake_minimum_required(VERSION 2.8)
project(hi_chip_info C)

# set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-std=gnu99")

include_directories(
    ./src/
)

add_executable(hi_chip_info
    src/main.c
    src/chipid.h
    src/chipid.c
    src/fake_symbols.c
    src/tools.h
    src/tools.c
)
target_link_libraries( hi_chip_info dl )
install(TARGETS hi_chip_info RUNTIME DESTINATION /usr/bin/)

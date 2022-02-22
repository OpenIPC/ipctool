cmake_minimum_required(VERSION 2.8.12)
project(ipctool C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-std=gnu99")

cmake_policy(SET CMP0069 NEW)

if(NOT BUILD_SHARED_LIBS)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_C_FLAGS "-static ${CMAKE_C_FLAGS}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s -Os -ffunction-sections -Wl,--gc-sections -DNDEBUG")
endif()

if(NOT SKIP_VERSION)
    set(VERSION_SRC ${CMAKE_CURRENT_BINARY_DIR}/version.c)
    # Add a custom command that produces version.c, plus
    # a dummy output that's not actually produced, in order
    # to force version.cmake to always be re-run before the build
    add_custom_command(
        OUTPUT ${VERSION_SRC}
               ${CMAKE_CURRENT_BINARY_DIR}/_version.c
        COMMAND ${CMAKE_COMMAND} -P
                ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake)
else()
    add_definitions(-DSKIP_VERSION)
endif()

include_directories(
    ./src/
)

set(COMMON_LIB_SRC
    src/hal_common.c
    src/hal_common.h
    src/hal_fh.c
    src/hal_fh.h
    src/hal_gm.c
    src/hal_gm.h
    src/hal_novatek.c
    src/hal_novatek.h
    src/hal_sstar.c
    src/hal_sstar.h
    src/hal_xm.c
    src/hal_xm.h
    src/hisi/hal_hisi.c
    src/hisi/hal_hisi.h

    src/chipid.c
    src/chipid.h
    src/hwinfo.c
    src/hwinfo.h
    src/mmap.h
    src/sensors.c
    src/sensors.h
    src/tools.c
    src/tools.h
    src/version.h
    )

set(IPCTOOL_SRC
    src/backup.c
    src/backup.h
    src/dns.c
    src/dns.h
    src/ethernet.c
    src/ethernet.h
    src/fake_symbols.c
    src/firmware.c
    src/firmware.h
    src/hashtable.c
    src/hashtable.h
    src/hisi/ethernet.c
    src/hisi/ethernet.h
    src/hisi/ispreg.c
    src/hisi/ispreg.h
    src/hisi/ptrace.c
    src/hisi/ptrace.h
    src/http.c
    src/http.h
    src/i2cspi.c
    src/i2cspi.h
    src/main.c
    src/mtd.c
    src/mtd.h
    src/network.c
    src/network.h
    src/ptrace.c
    src/ptrace.h
    src/ram.c
    src/ram.h
    src/reginfo.c
    src/reginfo.h
    src/sha1.c
    src/sha1.h
    src/snstool.c
    src/snstool.h
    src/stack.c
    src/uboot.c
    src/uboot.h
    src/watchdog.c
    src/watchdog.h

    src/cjson/cJSON.c
    src/cjson/cJSON.h
    src/cjson/cYAML.c
    src/cjson/cYAML.h

    src/vendors/anjoy.c
    src/vendors/anjoy.h
    src/vendors/buildroot.c
    src/vendors/buildroot.h
    src/vendors/common.c
    src/vendors/common.h
    src/vendors/hankvision.c
    src/vendors/hankvision.h
    src/vendors/linux.c
    src/vendors/linux.h
    src/vendors/openwrt.c
    src/vendors/openwrt.h
    src/vendors/ruision.c
    src/vendors/ruision.h
    src/vendors/xm.c
    src/vendors/xm.h
    src/vendors/sstar.c
    src/vendors/sstar.h

    ${VERSION_SRC}
    )

add_library(ipchw STATIC
    ${COMMON_LIB_SRC}
    )
target_compile_definitions(ipchw PUBLIC STANDALONE_LIBRARY)
target_link_libraries(ipchw m)

if(NOT ONLY_LIBRARY)
    add_executable(ipctool
        ${IPCTOOL_SRC}
        ${COMMON_LIB_SRC}
    )

    target_link_libraries(ipctool m)
    install(TARGETS ipctool RUNTIME DESTINATION /usr/bin/)

    add_executable(ipcinfo
        example/ipcinfo.c
        ${VERSION_SRC}
        )
    target_include_directories(ipcinfo PUBLIC include)
    target_link_libraries(ipcinfo ipchw)
    install(TARGETS ipcinfo RUNTIME DESTINATION /usr/bin/)
endif()

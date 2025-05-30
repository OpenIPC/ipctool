cmake_minimum_required(VERSION 3.10)
project(ipctool C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "-std=gnu99")

cmake_policy(SET CMP0069 NEW)

if(NOT BUILD_SHARED_LIBS)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_C_FLAGS "-static ${CMAKE_C_FLAGS}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_C_FLAGS
      "${CMAKE_C_FLAGS} -s -Os -ffunction-sections -Wl,--gc-sections -DNDEBUG -Wextra")
else()
  if(BUILD_SHARED_LIBS)
    set(CMAKE_C_FLAGS
        "${CMAKE_C_FLAGS} -O1 -g -fno-omit-frame-pointer")
  endif()
endif()

if(NOT SKIP_VERSION)
  set(VERSION_SRC ${CMAKE_CURRENT_BINARY_DIR}/version.c)
  # Add a custom command that produces version.c, plus a dummy output that's not
  # actually produced, in order to force version.cmake to always be re-run
  # before the build
  add_custom_command(
    OUTPUT ${VERSION_SRC} ${CMAKE_CURRENT_BINARY_DIR}/_version.c
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake)
else()
  add_definitions(-DSKIP_VERSION)
endif()

include_directories(./src/)

set(COMMON_LIB_SRC
    src/chipid.c
    src/chipid.h
    src/cjson/cJSON.c
    src/cjson/cJSON.h
    src/hal/allwinner.c
    src/hal/allwinner.h
    src/hal/bcm.c
    src/hal/bcm.h
    src/hal/common.c
    src/hal/common.h
    src/hal/fh.c
    src/hal/fh.h
    src/hal/gm.c
    src/hal/gm.h
    src/hal/hisi/hal_hisi.c
    src/hal/hisi/hal_hisi.h
    src/hal/hisi/ispreg.c
    src/hal/ingenic.c
    src/hal/ingenic.h
    src/hal/novatek.c
    src/hal/novatek.h
    src/hal/rockchip.c
    src/hal/rockchip.h
    src/hal/sstar.c
    src/hal/sstar.h
    src/hal/tegra.c
    src/hal/tegra.h
    src/hal/xilinx.c
    src/hal/xilinx.h
    src/hal/xm.c
    src/hal/xm.h
    src/hwinfo.c
    src/hwinfo.h
    src/mmap.h
    src/sensors.c
    src/sensors.h
    src/tools.c
    src/tools.h
    src/version.h)

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
    src/hal/hisi/ethernet.c
    src/hal/hisi/ethernet.h
    src/hal/hisi/ispreg.c
    src/hal/hisi/ispreg.h
    src/hal/hisi/ptrace.c
    src/hal/hisi/ptrace.h
    src/hashtable.c
    src/hashtable.h
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
    #src/stack.c
    src/uboot.c
    src/uboot.h
    src/watchdog.c
    src/watchdog.h
    src/cjson/cJSON.c
    src/cjson/cJSON.h
    src/cjson/cYAML.c
    src/cjson/cYAML.h
    src/boards/anjoy.c
    src/boards/anjoy.h
    src/boards/buildroot.c
    src/boards/buildroot.h
    src/boards/common.c
    src/boards/common.h
    src/boards/hankvision.c
    src/boards/hankvision.h
    src/boards/linux.c
    src/boards/linux.h
    src/boards/openwrt.c
    src/boards/openwrt.h
    src/boards/ruision.c
    src/boards/ruision.h
    src/boards/xm.c
    src/boards/xm.h
    src/boards/sstar.c
    src/boards/sstar.h
    ${VERSION_SRC})

set(CYAML_TEST_SRC
    src/cjson/cYAML_test.c
    src/cjson/cJSON.c
    src/cjson/cJSON.h
    src/cjson/cYAML.c
    src/cjson/cYAML.h)

add_library(ipchw STATIC ${COMMON_LIB_SRC})
target_compile_definitions(ipchw PUBLIC STANDALONE_LIBRARY)
target_link_libraries(ipchw m)

if(NOT ONLY_LIBRARY)
  add_executable(ipctool ${IPCTOOL_SRC} ${COMMON_LIB_SRC})

  target_link_libraries(ipctool m)
  install(TARGETS ipctool RUNTIME DESTINATION /usr/bin/)

  add_executable(ipcinfo example/ipcinfo.c src/tools.c ${VERSION_SRC})
  target_include_directories(ipcinfo PUBLIC include)
  target_link_libraries(ipcinfo ipchw)
  install(TARGETS ipcinfo RUNTIME DESTINATION /usr/bin/)

  add_executable(cYAML_test ${CYAML_TEST_SRC})
endif()

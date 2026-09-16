cmake_minimum_required(VERSION 3.10)
project(ipctool C)

set(CMAKE_C_STANDARD 99)
# -Werror=return-type in every configuration: a function that falls off its
# end returns whatever is in the register, and the callers of a bool helper
# then branch on it. That is not a warning anyone would notice in a build log
# and the default flags below carry -Wextra but not -Wall, which is where
# -Wreturn-type lives.
set(CMAKE_C_FLAGS "-std=gnu99 -Werror=return-type")

# sensors.c serialises sensor detection with a pthread mutex. On musl and
# glibc >= 2.34 the pthread symbols live in libc, but older glibc and uClibc
# need the explicit link flag, so ask for it rather than rely on the toolchain.
find_package(Threads REQUIRED)

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

# Vendor HALs are only reachable through the detection table in chipid.c, so a
# consumer that already knows its SoC family can drop the ones it can never run
# on. The HiSilicon HAL stays in unconditionally — chipid.c dispatches to it
# directly rather than through the table. Affects libipchw only; the ipctool
# executable is a diagnostic tool and always carries every vendor.
set(IPCHW_OPTIONAL_VENDORS
    allwinner bcm fh gm ingenic novatek rockchip sstar tegra xilinx xm)
set(IPCHW_VENDORS "all" CACHE STRING
    "Vendor HALs to compile into libipchw: 'all', 'none', or a ;-separated \
subset of: ${IPCHW_OPTIONAL_VENDORS}")

if(IPCHW_VENDORS STREQUAL "all")
  set(_ipchw_selected ${IPCHW_OPTIONAL_VENDORS})
elseif(IPCHW_VENDORS STREQUAL "none")
  set(_ipchw_selected "")
else()
  set(_ipchw_selected ${IPCHW_VENDORS})
endif()

# Which SoC families' pad-mux tables go into libipchw. They are 57 KB of
# hand-entered data all told, and regs_by_chip() names every one of them from a
# single switch, so --gc-sections cannot drop any: a consumer has to say what it
# targets or pay for the lot. The ipctool executable always carries every table.
#
# Same shape as IPCHW_VENDORS above, and for the same reason -- set(... CACHE)
# here means a parent project has to FORCE it.
set(IPCHW_PADMUX_FAMILIES v1 v2 v2a v3 v3a v4 v4a v5 3536c 3536d sstar
    ingenic)
set(IPCHW_PADMUX "all" CACHE STRING
    "Pad-mux tables compiled into libipchw: 'all', 'none', or a subset of ${IPCHW_PADMUX_FAMILIES}")

if(IPCHW_PADMUX STREQUAL "all")
  set(_padmux_selected ${IPCHW_PADMUX_FAMILIES})
elseif(IPCHW_PADMUX STREQUAL "none")
  set(_padmux_selected "")
else()
  set(_padmux_selected ${IPCHW_PADMUX})
endif()

foreach(_f IN LISTS IPCHW_PADMUX_FAMILIES)
  string(TOUPPER ${_f} _F)
  list(APPEND IPCHW_PADMUX_ALL_DEFS IPCHW_PADMUX_${_F})
endforeach()

foreach(_f IN LISTS _padmux_selected)
  if(NOT _f IN_LIST IPCHW_PADMUX_FAMILIES)
    message(FATAL_ERROR
            "IPCHW_PADMUX: unknown family '${_f}'; "
            "expected 'all', 'none', or a subset of ${IPCHW_PADMUX_FAMILIES}")
  endif()
  string(TOUPPER ${_f} _F)
  list(APPEND IPCHW_PADMUX_DEFS IPCHW_PADMUX_${_F})
endforeach()
message(STATUS "libipchw pad-mux tables: ${_padmux_selected}")

# The two vendors whose pad-mux is its own backend rather than a table in
# reginfo.c. Selecting the family is what compiles the backend in; selecting
# the vendor HAL is what makes chip_generation reach it. They are separate
# questions, which is why they are separate knobs -- a consumer can carry the
# SigmaStar tables in a build that detects nothing else, which is exactly what
# reginfo_test does.
set(IPCHW_PADMUX_BACKENDS "")
foreach(_f sstar ingenic)
  if(_f IN_LIST _padmux_selected)
    list(APPEND IPCHW_PADMUX_BACKENDS src/hal/${_f}_padmux.c)
  endif()
endforeach()
set(IPCHW_PADMUX_BACKENDS_ALL src/hal/sstar_padmux.c src/hal/ingenic_padmux.c)

set(COMMON_LIB_SRC_BASE
    src/chipid.c
    src/chipid.h
    src/cjson/cJSON.c
    src/cjson/cJSON.h
    src/hal/common.c
    src/hal/common.h
    src/hal/hisi/hal_hisi.c
    src/hal/hisi/hal_hisi.h
    src/hal/hisi/ispreg.c
    src/hwinfo.c
    src/hwinfo.h
    src/mmap.h
    src/padmux.c
    src/padmux.h
    src/reginfo.c
    src/reginfo.h
    src/sensors.c
    src/sensors.h
    src/tools.c
    src/tools.h
    src/version.h)

# Full set, used by the ipctool executable
set(COMMON_LIB_SRC_ALL ${COMMON_LIB_SRC_BASE} ${IPCHW_PADMUX_BACKENDS_ALL})
foreach(_v IN LISTS IPCHW_OPTIONAL_VENDORS)
  list(APPEND COMMON_LIB_SRC_ALL src/hal/${_v}.c src/hal/${_v}.h)
  string(TOUPPER ${_v} _V)
  list(APPEND IPCHW_ALL_VENDOR_DEFS IPCHW_VENDOR_${_V})
endforeach()

# Selected subset, used by libipchw
set(COMMON_LIB_SRC ${COMMON_LIB_SRC_BASE} ${IPCHW_PADMUX_BACKENDS})
foreach(_v IN LISTS _ipchw_selected)
  if(NOT _v IN_LIST IPCHW_OPTIONAL_VENDORS)
    message(FATAL_ERROR
            "IPCHW_VENDORS: unknown vendor '${_v}'; "
            "expected 'all', 'none', or a subset of ${IPCHW_OPTIONAL_VENDORS}")
  endif()
  list(APPEND COMMON_LIB_SRC src/hal/${_v}.c src/hal/${_v}.h)
  string(TOUPPER ${_v} _V)
  list(APPEND IPCHW_VENDOR_DEFS IPCHW_VENDOR_${_V})
endforeach()
message(STATUS "libipchw vendor HALs: hisi ${_ipchw_selected}")

set(IPCTOOL_SRC
    src/backup.c
    src/backup.h
    src/bootrom.c
    src/bootrom.h
    src/clocks.c
    src/clocks.h
    src/cpubench.c
    src/cpubench.h
    src/crypto/aes.c
    src/crypto/aes.h
    src/crypto/chachapoly.c
    src/crypto/chachapoly.h
    src/crypto/gcm.c
    src/crypto/gcm.h
    src/crypto/hisi_cipher.c
    src/crypto/hisi_cipher.h
    src/cryptobench.c
    src/cryptobench.h
    src/dns.c
    src/dns.h
    src/ethernet.c
    src/ethernet.h
    src/fake_symbols.c
    src/firmware.c
    src/firmware.h
    src/hal/hisi/clocks_v4.c
    src/hal/hisi/clocks_v4a.c
    src/hal/hisi/clocks_v5.c
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
    src/membw.c
    src/membw.h
    src/mtd.c
    src/mtd.h
    src/network.c
    src/network.h
    src/ptrace.c
    src/ptrace.h
    src/ram.c
    src/ram.h
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
    src/boards/sstar.h)

set(CYAML_TEST_SRC
    src/cjson/cYAML_test.c
    src/cjson/cJSON.c
    src/cjson/cJSON.h
    src/cjson/cYAML.c
    src/cjson/cYAML.h)

add_library(ipchw STATIC ${COMMON_LIB_SRC})
# include/ipchw.h is the published header; carry it on the target so a
# consumer that links this library can find it without naming the path.
target_include_directories(ipchw PUBLIC
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
# PUBLIC on purpose: a consumer that trimmed a family out can #error on the
# missing macro at compile time rather than meet IPCHW_PADMUX_NO_TABLE on a
# camera.
target_compile_definitions(ipchw PUBLIC STANDALONE_LIBRARY ${IPCHW_VENDOR_DEFS}
                           ${IPCHW_PADMUX_DEFS})
# Threads is carried on the interface so anything linking this static library
# (ipcinfo here, and external consumers of libipchw) pulls pthread in too.
target_link_libraries(ipchw m Threads::Threads)

if(NOT ONLY_LIBRARY)
  # version.c gets exactly one owner. Listing a generated file in the sources
  # of both ipctool and ipcinfo put a copy of its rule in *each* target's
  # build.make, and `make -jN` runs those two sub-makes independently and at
  # the same time -- instrumented, the two version.cmake instances started
  # 2ms apart. Each then rewrites version.c while the other target's compiler
  # may be reading it, so a parallel build failed now and then with
  # "version.c: No such file or directory"; -j1 was always clean, and CI
  # builds without -j, so it only ever bit local builds. add_custom_command's
  # own documentation says not to list an output in more than one independent
  # target. An object library compiles it once and both executables link that.
  if(SKIP_VERSION)
    set(VERSION_OBJ "")
  else()
    add_library(ipctool_version OBJECT ${VERSION_SRC})
    set(VERSION_OBJ $<TARGET_OBJECTS:ipctool_version>)
  endif()

  add_executable(ipctool ${IPCTOOL_SRC} ${COMMON_LIB_SRC_ALL} ${VERSION_OBJ})
  target_include_directories(ipctool PRIVATE include)
  target_compile_definitions(ipctool PRIVATE ${IPCHW_ALL_VENDOR_DEFS}
                             ${IPCHW_PADMUX_ALL_DEFS})

  target_link_libraries(ipctool m Threads::Threads)
  install(TARGETS ipctool RUNTIME DESTINATION /usr/bin/)

  add_executable(ipcinfo example/ipcinfo.c src/tools.c ${VERSION_OBJ})
  target_include_directories(ipcinfo PUBLIC include)
  target_link_libraries(ipcinfo ipchw)
  install(TARGETS ipcinfo RUNTIME DESTINATION /usr/bin/)

  add_executable(cYAML_test ${CYAML_TEST_SRC})

  # Linking the real libipchw rather than recompiling reginfo.c is the point:
  # it proves the lookups are actually exported from the library, not just
  # present in the executable.
  add_executable(reginfo_test src/reginfo_test.c)
  target_include_directories(reginfo_test PRIVATE include src)
  target_link_libraries(reginfo_test ipchw)
endif()

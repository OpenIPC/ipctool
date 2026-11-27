cmake_minimum_required(VERSION 3.10)

# Standalone configuration does not need a target libc for compiler checks.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  project(nt98513_diagnostics C)
endif()

# The parent intentionally sets global -s/--gc-sections/Release flags. Keep
# these freestanding tools independent without changing the parent's scope.
set(CMAKE_C_FLAGS "")
set(CMAKE_ASM_FLAGS "")
set(CMAKE_EXE_LINKER_FLAGS "")
foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER "${_config}" _upper)
  set(CMAKE_C_FLAGS_${_upper} "")
  set(CMAKE_ASM_FLAGS_${_upper} "")
  set(CMAKE_EXE_LINKER_FLAGS_${_upper} "")
endforeach()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang)$")
  message(FATAL_ERROR "NT98513 diagnostics require a GNU/Clang Linux MIPS little-endian O32 toolchain")
endif()

set(_nt98513_flags -EL -march=mips32r2 -mabi=32 -msoft-float -mno-abicalls
    -fno-pic -G0 -ffreestanding -fno-builtin -fno-stack-protector -O2)
include(CheckCSourceCompiles)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
string(REPLACE ";" " " CMAKE_REQUIRED_FLAGS "${_nt98513_flags}")
set(CMAKE_REQUIRED_LIBRARIES "")
set(CMAKE_REQUIRED_INCLUDES "")
set(CMAKE_REQUIRED_DEFINITIONS "")
# Check actual compiler macros on every enabled configure, never host-name hints.
unset(NT98513_COMPILER_ABI_OK CACHE)
check_c_source_compiles("
#if !defined(__mips__) || !defined(__MIPSEL__) || !defined(__mips_soft_float)
#error incompatible target
#endif
#if __mips != 32 || __mips_isa_rev != 2 || _MIPS_SIM != 1 || __SIZEOF_POINTER__ != 4
#error incompatible MIPS ISA or ABI
#endif
int nt98513_compiler_contract(void) { return 0; }
" NT98513_COMPILER_ABI_OK)
if(NOT NT98513_COMPILER_ABI_OK)
  message(FATAL_ERROR "NT98513 diagnostics require a compiler accepting little-endian MIPS32r2 O32 soft-float freestanding flags; use a compatible toolchain file")
endif()
enable_language(ASM)

set(_nt98513_client src/nt98513_control.c src/nt98513_client.c
    src/nt98513_client_cli.c src/nt98513_client_mips.c src/nt98513_client_mips.S)
add_executable(nt98513-descriptor-probe src/nt98513_stream.c src/nt98513_probe.c
    src/nt98513_probe_mips.c src/nt98513_probe_mips.S)
add_executable(nt98513-client ${_nt98513_client})
add_executable(nt98513-sensor ${_nt98513_client} src/nt98513_sensor.c
    src/nt98513_sensor_query.c src/nt98513_sensor_cli.c src/nt98513_sensor_cli_mips.c)
target_compile_definitions(nt98513-sensor PRIVATE
    NT98513_CLIENT_ENTRY=nt98513_sensor_cli_main)

foreach(_target nt98513-descriptor-probe nt98513-client nt98513-sensor)
  target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_options(${_target} PRIVATE ${_nt98513_flags} -Wall -Wextra -Werror)
  set_target_properties(${_target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO LINK_FLAGS "-nostdlib -static -no-pie -Wl,-e,_start,--build-id=none")
  install(TARGETS ${_target} RUNTIME DESTINATION bin COMPONENT NT98513Diagnostics)
endforeach()
install(FILES README.md LICENSE DESTINATION share/doc/nt98513-diagnostics
    COMPONENT NT98513Diagnostics)

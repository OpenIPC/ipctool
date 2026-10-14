# Optional NT98513 diagnostics

These source-owned diagnostics use recovered Linux MIPS O32 interfaces. They
are separate executables, not ipctool hardware detection, a bootable OpenIPC
platform, a capture backend or a service. The twenty C/header/assembly files are
unchanged from the independently reviewed diagnostic source bundle. The full
MIT notice is in LICENSE; no vendor firmware, SDK, module or tuning data is
included.

## Build

The parent option `BUILD_NT98513_DIAGNOSTICS` defaults to `OFF`. Leaving it off
adds no diagnostic targets, ASM compiler check, library or download dependency.
The configure commands shown below need CMake 3.13+ (validated with 3.25.1),
a build tool and a compatible GNU or Clang
Linux MIPS compiler with its freestanding stdint/stddef headers. It does not use
Docker, install a compiler or download anything. For an installed GCC in PATH:

```sh
cmake -S tools/nt98513 -B build-nt98513 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mipsel-linux-gnu.cmake
cmake --build build-nt98513
```

Use a caller-controlled new build directory. A custom toolchain file may set
compiler paths, compiler target and `CMAKE_SYSROOT` as usual. Target compilation
checks actual compiler macros with explicit little-endian MIPS32r2, O32 and
soft-float flags. Unsupported enabled configurations fail at configuration.
The tools are freestanding, static and have no libc/startup linkage. Their
accepted syscall entries supply `_start`. Local build flags isolate them from
the parent's stripping, garbage collection and optimization flags; the parent
flags and existing targets are not changed.

A parent build may opt in with `-DBUILD_NT98513_DIAGNOSTICS=ON` and the same
compatible compiler. Unlike the standalone subtree, configuring/building the
parent also needs its existing libc, pthread and other platform dependencies.
A successful subtree build does not establish that those dependencies exist.
The example toolchain selects a compiler; it is not a matching camera BSP.

## Private staging

Install only this component to a caller-selected empty private staging root:

```sh
DESTDIR=/private/work/stage cmake -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_COMPONENT=NT98513Diagnostics \
  -P build-nt98513/cmake_install.cmake
```

Standard CMake installation semantics apply: it can replace existing files.
Choose a new private staging root and never use the host root for offline
validation. No install happens during configure or build. Relative destinations
are `bin/nt98513-descriptor-probe`, `bin/nt98513-client`, `bin/nt98513-sensor`
and `share/doc/nt98513-diagnostics/{README.md,LICENSE}` under the prefix. The
component avoids invoking the parent's other install rules; those are unchanged.

## Operations and limits

No arguments or `--help` prints usage without device I/O. The three tools are:

* `nt98513-descriptor-probe --descriptors CHANNEL COUNT TIMEOUT_MS`: descriptor
  and range metadata only; no payload mapping, copy or ownership lease.
* `nt98513-client command TEXT`, `get TEXT BYTE_COUNT`, or `set TEXT HEX_BYTES`:
  one explicit raw transaction. Arbitrary commands can mutate camera state.
* `nt98513-sensor active`: one fixed query, typed JSON and distinct CMOS_OFF.
  The result is mutable RTOS software selection, not fitted hardware/readiness.

The sensor entry selects `nt98513_sensor_cli_main`. It deliberately retains the
generic client CLI/main and its 40,960-byte workspace because the unchanged
adapter references them. The client and probe select their own existing mains.

The fixed sensor command clears shared usys staging. Prezeroed reply buffers
have no remote produced-text-length receipt; bounded text is not proof of fresh
complete remote production. Driver calls can block indefinitely; the probe's
nonzero timeout is not a whole-operation deadline. Output may be partial on I/O
failure, and process termination can interrupt cleanup. Compatible kernel/RTOS/
module initialization, permissions, address semantics and watchdog ownership
remain external prerequisites. Build and isolated missing-device smoke do not
validate target compatibility, cold boot, physical clocks, DMA visibility,
payload lifetime, capture or recovery. No device should be passed into the
offline validation environment. No default command, autostart, module/node setup
or firmware installation is supplied.

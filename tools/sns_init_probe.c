// Tiny wrapper that loads a libsns_*.so directly and calls its sensor
// init function, without pulling in the rest of the streamer. Lets us
// exercise the .so's I2C path under ipctool trace in isolation.
//
// Build for ARM with the OpenIPC toolchain:
//   arm-openipc-linux-musleabi-gcc -O2 -static \
//       tools/sns_init_probe.c -ldl -lpthread \
//       -o sns_init_probe
//
// Run on the camera (after killing the streamer so the I2C bus is free):
//   killall majestic
//   sns_init_probe /usr/lib/sensors/libsns_jxf22.so sensor_linear_1080p30_init
//
// Or under trace:
//   ipctool trace --output=cap.log sns_init_probe /usr/lib/sensors/libsns_jxf22.so sensor_init
//
// We accept either symbol name; if neither is exported we fall back to
// sensor_init (the SDK glue most drivers expose).
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*init_fn_int)(int);
typedef void (*init_fn_void)(int);

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <path to libsns_xxx.so> [symbol]\n"
                "  default symbol search: sensor_linear_1080p30_init,\n"
                "                         sensor_init, sensor_i2c_init\n",
                argv[0]);
        return 2;
    }

    const char *so = argv[1];
    void *h = dlopen(so, RTLD_NOW);
    if (!h) {
        fprintf(stderr, "dlopen(%s) failed: %s\n", so, dlerror());
        return 1;
    }

    const char *try_syms[4];
    int nsyms = 0;
    if (argc >= 3) {
        try_syms[nsyms++] = argv[2];
    } else {
        try_syms[nsyms++] = "sensor_linear_1080p30_init";
        try_syms[nsyms++] = "sensor_init";
        try_syms[nsyms++] = "sensor_i2c_init";
    }

    void *fn = NULL;
    const char *sym = NULL;
    for (int i = 0; i < nsyms; i++) {
        dlerror();
        fn = dlsym(h, try_syms[i]);
        if (fn) {
            sym = try_syms[i];
            break;
        }
    }
    if (!fn) {
        fprintf(stderr, "no init symbol found in %s\n", so);
        return 1;
    }

    fprintf(stderr, "[probe] %s @ %p — calling\n", sym, fn);

    // First always do sensor_i2c_init to set up the bus, in case the user
    // asked for sensor_linear_1080p30_init directly (which doesn't open
    // the i2c device).
    void *i2c_init = dlsym(h, "sensor_i2c_init");
    if (i2c_init && i2c_init != fn) {
        ((init_fn_void)i2c_init)(0);
    }

    // Some symbols return int, some void. Calling void as int is harmless
    // on ARM EABI (return value just goes unused).
    int ret = ((init_fn_int)fn)(0);
    fprintf(stderr, "[probe] %s returned %d\n", sym, ret);

    void *i2c_exit = dlsym(h, "sensor_i2c_exit");
    if (i2c_exit) {
        ((init_fn_void)i2c_exit)(0);
    }

    dlclose(h);
    return 0;
}

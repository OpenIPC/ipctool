#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "backup.h"
#include "dns.h"
#include "http.h"

void do_backup(const char *yaml, size_t yaml_len) {
    printf("do_backup()\n");

    nservers_t ns;
    ns.len = 0;

    if (!parse_resolv_conf(&ns)) {
        fprintf(stderr, "parse_resolv_conf failed\n");
    }
    add_predefined_ns(&ns, 0xd043dede /* 208.67.222.222 of OpenDNS */,
                      0x01010101 /* 1.1.1.1 of Cloudflare */, 0);
    print_nservers(&ns);

    printf("download = %d\n", download("ifconfig.me", "", &ns, STDOUT_FILENO));

    // printf("%d\n", upload("camware.s3.eu-north-1.amazonaws.com", "test",
    // yaml,
    //                      yaml_len));
    printf("~do_backup()\n");
}

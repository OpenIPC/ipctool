#include <stdio.h>
#include <string.h>

#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "network.h"

bool get_mac_address(char *outbuf, ssize_t len) {
    struct ifreq ifr;
    struct ifconf ifc;
    char buf[1024];
    bool success = false;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock == -1)
        return false;

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1)
        return false;

    struct ifreq *it = ifc.ifc_req;
    const struct ifreq *const end = it + (ifc.ifc_len / sizeof(struct ifreq));

    for (; it != end; ++it) {
        strcpy(ifr.ifr_name, it->ifr_name);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
            if (!(ifr.ifr_flags & IFF_LOOPBACK)) { // don't count loopback
                if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                    success = true;
                    break;
                }
            }
        } else
            return false;
    }
    if (!success)
        return false;

    snprintf(outbuf, len, "%02hx:%02hx:%02hx:%02hx:%02hx:%02hx",
             ifr.ifr_hwaddr.sa_data[0], ifr.ifr_hwaddr.sa_data[1],
             ifr.ifr_hwaddr.sa_data[2], ifr.ifr_hwaddr.sa_data[3],
             ifr.ifr_hwaddr.sa_data[4], ifr.ifr_hwaddr.sa_data[5]);
    return true;
}

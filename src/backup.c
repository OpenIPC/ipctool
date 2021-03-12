#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "backup.h"

#define ERR_OK 0
#define ERR_GENERAL 1
#define ERR_SOCKET 2
#define ERR_GETADDRINFO 3
#define ERR_CONNECT 4
#define ERR_SEND 5
#define ERR_USAGE 6

#undef NDEBUG

typedef struct {
    uint16_t xid;     /* Randomly chosen identifier */
    uint16_t flags;   /* Bit-mask to indicate request/response */
    uint16_t qdcount; /* Number of questions */
    uint16_t ancount; /* Number of answers */
    uint16_t nscount; /* Number of authority records */
    uint16_t arcount; /* Number of additional records */
} dns_header_t;

typedef struct {
    char *name;        /* Pointer to the domain name in memory */
    uint16_t dnstype;  /* The QTYPE (1 = A) */
    uint16_t dnsclass; /* The QCLASS (1 = IN) */
} dns_question_t;

/* Structure of the bytes for an IPv4 answer */
typedef struct {
    uint16_t compression;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t length;
    struct in_addr addr;
} __attribute__((packed)) dns_record_a_t;

static int get_http_respcode(const char *inpbuf) {
    char proto[4096], descr[4096];
    int code;

    if (sscanf(inpbuf, "%s %d %s", proto, &code, descr) < 2)
        return -1;
    return code;
}

static int resolv_name(const char *hostname) {
    int socketfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    /* OpenDNS is currently at 208.67.222.222 (0xd043dede) */
    address.sin_addr.s_addr = htonl(0xd043dede);
    /* DNS runs on port 53 */
    address.sin_port = htons(53);

    /* Set up the DNS header */
    dns_header_t header;
    memset(&header, 0, sizeof(dns_header_t));
    header.xid = htons(0x1234);   /* Randomly chosen ID */
    header.flags = htons(0x0100); /* Q=0, RD=1 */
    header.qdcount = htons(1);    /* Sending 1 question */

    /* Set up the DNS question */
    dns_question_t question;
    question.dnstype = htons(1);  /* QTYPE 1=A */
    question.dnsclass = htons(1); /* QCLASS 1=IN */

    /* DNS name format requires two bytes more than the length of the
       domain name as a string */
    question.name = calloc(strlen(hostname) + 2, sizeof(char));

    /* Leave the first byte blank for the first field length */
    memcpy(question.name + 1, hostname, strlen(hostname));
    uint8_t *prev = (uint8_t *)question.name;
    uint8_t count = 0; /* Used to count the bytes in a field */

#if 0
/* Traverse through the name, looking for the . locations */
for (size_t i = 0; i < strlen (hostname); i++)
  {
    /* A . indicates the end of a field */
    if (hostname[i] == '.')
      {
        /* Copy the length to the byte before this field, then
           update prev to the location of the . */
        *prev = count;
        prev = query + i + 1;
        count = 0;
      }
    else
      count++;
  }
*prev = count;
#endif

    /* Copy all fields into a single, concatenated packet */
    size_t packetlen = sizeof(header) + strlen(hostname) + 2 +
                       sizeof(question.dnstype) + sizeof(question.dnsclass);
    uint8_t *packet = calloc(packetlen, sizeof(uint8_t));
    uint8_t *p = (uint8_t *)packet;

    /* Copy the header first */
    memcpy(p, &header, sizeof(header));
    p += sizeof(header);

    /* Copy the question name, QTYPE, and QCLASS fields */
    memcpy(p, &question.dnstype, sizeof(question.dnstype));
    p += sizeof(question.dnstype);
    memcpy(p, &question.dnsclass, sizeof(question.dnsclass));

    /* Send the packet to OpenDNS, then request the response */
    sendto(socketfd, packet, packetlen, 0, (struct sockaddr *)&address,
           (socklen_t)sizeof(address));

    socklen_t length = 0;
    uint8_t response[512];
    memset(&response, 0, 512);

    /* Receive the response from OpenDNS into a local buffer */
    ssize_t bytes = recvfrom(socketfd, response, 512, 0,
                             (struct sockaddr *)&address, &length);
    printf("Received %d DNS\n", bytes);

    dns_header_t *response_header = (dns_header_t *)response;
    assert((ntohs(response_header->flags) & 0xf) == 0);

    /* Get a pointer to the start of the question name, and
       reconstruct it from the fields */
    uint8_t *start_of_name = (uint8_t *)(response + sizeof(dns_header_t));
    uint8_t total = 0;
    uint8_t *field_length = start_of_name;
    while (*field_length != 0) {
        /* Restore the dot in the name and advance to next length */
        total += *field_length + 1;
        *field_length = '.';
        field_length = start_of_name + total;
    }
    *field_length = '\0'; /* Null terminate the name */
    puts(start_of_name);

    return 0;
}

static int upload(const char *hostname, const char *uri, const char *data,
                  size_t len) {
    int ret = ERR_GENERAL;

    struct addrinfo hints, *res, *res0;
    printf("HERE\n");
    resolv_name(hostname);

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo(hostname, "80", &hints, &res0);
    if (err) {
    }
    printf("HERE\n");

    int s = -1;
    for (res = res0; res; res = res->ai_next) {
        s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s < 0) {
            ret = ERR_SOCKET;
            continue;
        }

#ifndef NDEBUG
        char buf[256];
        inet_ntop(res->ai_family,
                  &((struct sockaddr_in *)res->ai_addr)->sin_addr, buf,
                  sizeof(buf));
        fprintf(stderr, "Connecting to %s...\n", buf);
#endif

        if (connect(s, res->ai_addr, res->ai_addrlen) < 0) {
            ret = ERR_CONNECT;
            close(s);
            s = -1;
            continue;
        }
        break; /* okay we got one */
    }
    freeaddrinfo(res0);

    if (s < 0) {
        return ret;
    }

    char buf[4096] = "PUT /";
    if (uri) {
        strncat(buf, uri, sizeof(buf) - strlen(buf) - 1);
    }
    strncat(buf, " HTTP/1.0\r\nHost: ", sizeof(buf) - strlen(buf) - 1);
    strncat(buf, hostname, sizeof(buf) - strlen(buf) - 1);
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf) - 1,
             "\r\n"
             "Content-type: application/octet-stream\r\n"
             "Connection: close\r\n"
             "Content-Length: %d\r\n"
             "\r\n",
             len);
    int tosent = strlen(buf);
    int nsent = send(s, buf, tosent, 0);
    puts(buf);
    if (nsent != tosent)
        return ERR_SEND;

    int n = write(s, data, len);
    printf("Sent %d bytes\n", n);

    return 0;
}

void do_backup(const char *yaml, size_t yaml_len) {
    printf("do_backup()\n");
    printf("%d\n", upload("camware.s3.eu-north-1.amazonaws.com", "test", yaml,
                          yaml_len));
    printf("~do_backup()\n");
}

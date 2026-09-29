#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "backup.h"
#include "report.h"
#include "version.h"

/*
 * ipctool upload: send this camera's report to openipc.org's board
 * catalogue, where it is reviewed before anything of it is published.
 *
 * Nothing leaves the camera unless this command is run, and it says what it
 * sends before it sends it (#78). By default that is the YAML ipctool prints
 * -- which openipc.org publishes with the MAC, die ID and cloud ID replaced
 * by hashes. A flash backup goes only with --backup, only after the owner
 * confirms, and is published only with --public.
 */

#define DEFAULT_HOST "openipc.org"
#define REPORTS_PATH "/api/v1/reports"

static void usage(void) {
    fprintf(stderr,
            "Usage: ipctool upload [--backup [--public]] [--yes] "
            "[--note TEXT] [--host NAME]\n"
            "  (none)       send the hardware report ipctool prints\n"
            "  --backup     also send a backup of the whole flash, kept "
            "private:\n"
            "               only OpenIPC's maintainers can read it\n"
            "  --public     let the backup be published with the report\n"
            "  --yes        do not ask (for scripts and agents)\n"
            "  --note TEXT  where the camera came from, what it is sold as\n"
            "  --host NAME[:PORT]\n"
            "               another openipc.org (dev.openipc.org), or a "
            "bench server\n");
}

static bool confirm(const char *what) {
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr,
                "Not sent: %s needs a yes, and there is no terminal to ask "
                "on. Run it with --yes if you mean it.\n",
                what);
        return false;
    }
    fprintf(stderr, "Type yes to send %s: ", what);
    char line[16] = "";
    if (!fgets(line, sizeof(line), stdin))
        return false;
    return !strcmp(line, "yes\n") || !strcmp(line, "yes");
}

int report_cmd(int argc, char **argv) {
    const struct option opts[] = {{"backup", no_argument, NULL, 'b'},
                                  {"public", no_argument, NULL, 'p'},
                                  {"yes", no_argument, NULL, 'y'},
                                  {"note", required_argument, NULL, 'n'},
                                  {"host", required_argument, NULL, 'H'},
                                  {"help", no_argument, NULL, 'h'},
                                  {NULL, 0, NULL, 0}};
    bool with_backup = false, public_backup = false, yes = false;
    const char *note = NULL, *host = DEFAULT_HOST;
    int c;
    optind = 1;
    while ((c = getopt_long(argc, argv, "bpyn:H:h", opts, NULL)) != -1) {
        switch (c) {
        case 'b':
            with_backup = true;
            break;
        case 'p':
            public_backup = true;
            break;
        case 'y':
            yes = true;
            break;
        case 'n':
            note = optarg;
            break;
        case 'H':
            host = optarg;
            break;
        default:
            usage();
            return EXIT_FAILURE;
        }
    }
    if (public_backup && !with_backup) {
        fprintf(stderr, "--public is about the backup: use it with --backup\n");
        return EXIT_FAILURE;
    }

    char *yaml = build_report_yaml();
    if (!yaml)
        return EXIT_FAILURE;
    size_t yaml_len = strlen(yaml);

    printf("%s", yaml);
    fprintf(stderr,
            "\nThis sends the report above to http://%s%s, over plain HTTP "
            "(stock camera firmware has no TLS).\n"
            "It is reviewed before it is published, and the MAC, chip ID and "
            "cloud ID in the report are replaced with hashes.\n",
            host, REPORTS_PATH);

    span_t blocks[MAX_MTDBLOCKS + 1];
    size_t nblocks = 0;
    size_t flash = 0;
    if (with_backup) {
        // Asked before anything is read: collecting the backup reads UBI
        // volumes into memory, and a "no" should leave them untouched.
        fprintf(stderr,
                "\nWith it goes a backup of the whole flash. It holds "
                "everything the camera keeps: its settings, Wi-Fi keys, "
                "passwords, cloud IDs.\n");
        if (public_backup)
            fprintf(
                stderr,
                "You chose --public: once reviewed, anyone can download "
                "this backup from openipc.org, and in it the MAC, chip ID, "
                "cloud ID and everything else on the flash are in clear.\n");
        else
            fprintf(stderr, "It is kept private: only OpenIPC's maintainers "
                            "can read it. Add --public to share it.\n");
        if (!yes && !confirm(public_backup ? "a public backup of this camera"
                                           : "a private backup of this "
                                             "camera")) {
            fprintf(stderr, "Nothing was sent.\n");
            free(yaml);
            return EXIT_FAILURE;
        }
        size_t missed = 0;
        nblocks = backup_blocks(yaml, yaml_len, blocks, &missed);
        for (size_t i = 1; i < nblocks; i++)
            flash += blocks[i].len;
        if (missed || nblocks < 2) {
            fprintf(stderr,
                    "Not sent: %zu partition(s) of the flash could not be "
                    "read, and a backup missing any of them is not the whole "
                    "flash.\n",
                    missed ? missed : (size_t)1);
            free(yaml);
            return EXIT_FAILURE;
        }
        fprintf(stderr, "Backing up %zu partitions, %zu KB.\n", nblocks - 1,
                flash >> 10);
    }

    char boundary[48];
    snprintf(boundary, sizeof(boundary), "ipctool-%08lx%08lx",
             (unsigned long)time(NULL), (unsigned long)getpid());
    report_body_t *body = calloc(1, sizeof(*body));
    if (!body) {
        free(yaml);
        return EXIT_FAILURE;
    }
    report_body_init(body, boundary);
    report_add_field(body, "channel", "ipctool");
    char tool[64];
#ifndef SKIP_VERSION
    snprintf(tool, sizeof(tool), "ipctool %s", get_git_version());
#else
    snprintf(tool, sizeof(tool), "ipctool");
#endif
    report_add_field(body, "tool", tool);
    if (note)
        report_add_field(body, "note", note);
    if (with_backup) {
        report_add_field(body, "consent", public_backup ? "public" : "private");
        report_add_backup(body, blocks, nblocks);
    } else {
        report_add_yaml(body, yaml, yaml_len);
    }
    report_body_finish(body);
    if (body->overflow) {
        fprintf(stderr, "The report has more parts than ipctool can send.\n");
        free(body);
        free(yaml);
        return EXIT_FAILURE;
    }

    nservers_t ns;
    ns.len = 0;
    parse_resolv_conf(&ns);
    add_predefined_ns(&ns, 0xd043dede /* 208.67.222.222 of OpenDNS */,
                      0x01010101 /* 1.1.1.1 of Cloudflare */, 0);

    /* --host name[:port], for a bench server or an agent's receiver. */
    char hostname[256];
    int port = 80;
    snprintf(hostname, sizeof(hostname), "%s", host);
    char *colon = strchr(hostname, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "--host %s: the port is not a number\n", host);
            free(body);
            free(yaml);
            return EXIT_FAILURE;
        }
    }

    char ctype[96];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s",
             boundary);
    static char resp[16384];
    int status = 0;
    int err = http_post(hostname, port, REPORTS_PATH, &ns, ctype, body->spans,
                        body->nspans, body->total, resp, sizeof(resp), &status);
    free(body);
    free(yaml);
    if (err) {
        fprintf(stderr, "Could not reach %s (error %d). Nothing was stored.\n",
                host, err);
        return EXIT_FAILURE;
    }
    report_answer_t a;
    report_parse_answer(status, resp, &a);
    if (status != 201 || !*a.id) {
        fprintf(stderr, "%s did not take the report (%d): %s\n", host, status,
                *a.error ? a.error : "no reason given");
        return EXIT_FAILURE;
    }
    printf("\nReceived as %s. Its state: %s\n", a.id, a.receipt_url);
    if (a.known)
        printf("The catalogue already knows this board: %s\n", a.match);
    else if (*a.match)
        printf("It may be: %s\n", a.match);
    return EXIT_SUCCESS;
}

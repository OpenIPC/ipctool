#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
 * by hashes. A flash backup goes only when the owner chooses it: asked at
 * the terminal after the report is shown, or with --backup, which needs a
 * yes too. It is published only when they say so (public, or --public).
 */

#define DEFAULT_HOST "openipc.org"
#define REPORTS_PATH "/api/v1/reports"

static void usage(void) {
    fprintf(stderr,
            "Usage: ipctool upload [--backup [--public] | --no-backup] "
            "[--yes] [--note TEXT] [--host NAME]\n"
            "  (none)       send the hardware report ipctool prints, and ask "
            "whether\n"
            "               to send a backup of the flash with it\n"
            "  --backup     also send a backup of the whole flash, kept "
            "private:\n"
            "               only OpenIPC's maintainers can read it\n"
            "  --public     let the backup be published with the report\n"
            "  --no-backup  send the report only, without asking\n"
            "  --yes        do not ask (for scripts and agents)\n"
            "  --note TEXT  where the camera came from, what it is sold as\n"
            "  --host NAME[:PORT]\n"
            "               another openipc.org (dev.openipc.org), or a "
            "bench server\n");
}

/* A line from the terminal, without its line end: telnet sends \r\n. */
static bool read_answer(char *line, size_t size) {
    if (!fgets(line, size, stdin))
        return false;
    line[strcspn(line, "\r\n")] = '\0';
    return true;
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
    return read_answer(line, sizeof(line)) && !strcmp(line, "yes");
}

/*
 * Offered once the report is on the screen, so the owner decides with it in
 * front of them. Anything but "private" or "public" is a no: the report goes
 * alone.
 */
static void ask_backup(bool *with_backup, bool *public_backup) {
    fprintf(stderr,
            "\nA backup of the whole flash, sent with the report, is what "
            "porting OpenIPC\n"
            "to this board starts from. It holds everything the camera "
            "keeps: its\n"
            "settings, Wi-Fi keys, passwords, cloud IDs.\n"
            "  no       send the report only\n"
            "  private  send the backup too; only OpenIPC's maintainers can "
            "read it\n"
            "  public   send the backup too, and let anyone download it once "
            "the\n"
            "           report is reviewed, with the MAC, chip ID and cloud "
            "ID in clear\n"
            "Send a backup? [no/private/public]: ");
    char line[16] = "";
    if (!read_answer(line, sizeof(line)))
        line[0] = '\0';
    if (!strcmp(line, "private") || !strcmp(line, "public")) {
        *with_backup = true;
        *public_backup = !strcmp(line, "public");
    } else {
        fprintf(stderr, "No backup: sending the report only.\n");
    }
}

int report_cmd(int argc, char **argv) {
    const struct option opts[] = {{"backup", no_argument, NULL, 'b'},
                                  {"public", no_argument, NULL, 'p'},
                                  {"no-backup", no_argument, NULL, 'N'},
                                  {"yes", no_argument, NULL, 'y'},
                                  {"note", required_argument, NULL, 'n'},
                                  {"host", required_argument, NULL, 'H'},
                                  {"help", no_argument, NULL, 'h'},
                                  {NULL, 0, NULL, 0}};
    bool with_backup = false, public_backup = false, no_backup = false;
    bool yes = false, chosen = false;
    const char *note = NULL, *host = DEFAULT_HOST;
    int c;
    optind = 1;
    while ((c = getopt_long(argc, argv, "bpNyn:H:h", opts, NULL)) != -1) {
        switch (c) {
        case 'b':
            with_backup = true;
            break;
        case 'p':
            public_backup = true;
            break;
        case 'N':
            no_backup = true;
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
    if (with_backup && no_backup) {
        fprintf(stderr, "--backup and --no-backup: choose one\n");
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

    // Asked only of someone at a terminal who has not decided already: a
    // script or an agent (--yes, or no terminal) sends the report alone
    // unless it said --backup.
    if (!with_backup && !no_backup && !yes && isatty(STDIN_FILENO)) {
        ask_backup(&with_backup, &public_backup);
        chosen = with_backup;
    }

    span_t blocks[MAX_MTDBLOCKS + 1];
    size_t nblocks = 0;
    size_t flash = 0;
    if (with_backup && !chosen) {
        // Asked before anything is read: collecting the backup reads UBI
        // volumes into memory, and a "no" should leave them untouched. The
        // owner who answered ask_backup() has read this already.
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
    }
    if (with_backup) {
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
    char path[256] = REPORTS_PATH, location[512];
    int status = 0, err;
    /* A server may send the report elsewhere with a 307 or 308 -- openipc.org
     * does, for a network that cannot reach it -- and says so before the body
     * is sent (http_post), so following costs nothing but the hop.
     *
     * To another host only from openipc.org itself: the owner agreed to
     * send this report, maybe with the whole flash in it, to the host named
     * above, and where openipc.org forwards its own reports is part of that.
     * Any other server may move it only within its own name. */
    for (int hops = 0;; hops++) {
        err = http_post(hostname, port, path, &ns, ctype, body->spans,
                        body->nspans, body->total, resp, sizeof(resp), &status,
                        location, sizeof(location));
        if (err || (status != 307 && status != 308) || hops == 3)
            break;
        char next[256], nextpath[256];
        int nport;
        if (!report_redirect(location, next, sizeof(next), &nport, nextpath,
                             sizeof(nextpath)))
            break;
        if (strcasecmp(next, hostname) && strcasecmp(hostname, DEFAULT_HOST)) {
            fprintf(stderr,
                    "%s sends the report to %s, another host. It goes only "
                    "where it was asked to: --host %s sends it there.\n",
                    hostname, location, next);
            *location = '\0';
            break;
        }
        snprintf(path, sizeof(path), "%s", nextpath);
        fprintf(stderr, "%s sends the report to %s.\n", hostname, location);
        snprintf(hostname, sizeof(hostname), "%s", next);
        port = nport;
    }
    free(body);
    free(yaml);
    if (err == ERR_STALLED) {
        fprintf(stderr,
                "The upload to %s stopped moving and no answer came. Nothing "
                "was stored. Something between this camera and %s holds the "
                "connection; --host can send the report another way.\n",
                hostname, hostname);
        return EXIT_FAILURE;
    }
    if (err) {
        fprintf(stderr, "Could not reach %s (error %d). Nothing was stored.\n",
                hostname, err);
        return EXIT_FAILURE;
    }
    report_answer_t a;
    report_parse_answer(status, resp, &a);
    if (status != 201 || !*a.id) {
        if ((status == 307 || status == 308) && *location)
            fprintf(stderr,
                    "%s sends the report to %s, which ipctool cannot "
                    "follow.\n",
                    hostname, location);
        fprintf(stderr, "%s did not take the report (%d): %s\n", hostname,
                status, *a.error ? a.error : "no reason given");
        return EXIT_FAILURE;
    }
    printf("\nReceived as %s. Its state: %s\n", a.id, a.receipt_url);
    if (a.known)
        printf("The catalogue already knows this board: %s\n", a.match);
    else if (*a.match)
        printf("It may be: %s\n", a.match);
    return EXIT_SUCCESS;
}

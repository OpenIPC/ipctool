#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "cjson/cJSON.h"

#include "chipid.h"
#include "tools.h"
#include "vendors/xm.h"

bool is_xm_board() {
    if (!access("/proc/xm/xminfo", 0)) {
        strcpy(board_manufacturer, "Xiongmai");
        return true;
    }
    return false;
}

static void detect_nor_chip() {
    char buf[100];

    int fd = open("/dev/mtd0", 0x1002);
    if (fd < 0) {
        return;
    }

    // XMMTD_GETFLASHNAME
    memset(buf, 0, sizeof buf);
    if (ioctl(fd, 0x40044DAAu, &buf) >= 0) {
        sprintf(nor_chip, "      name: \"%s\"\n", buf);
    }

    // XMMTD_GETFLASHID
    uint32_t flash_id;
    if (ioctl(fd, 0x40044DA9u, &flash_id) >= 0) {
        sprintf(nor_chip + strlen(nor_chip), "      id: 0x%06x\n", flash_id);
    }

    close(fd);
}

bool xm_spiflash_checkpasswd(int fd, int password) {
    assert(fd);
    // XMMTD_CHECKPASSWD
    return ioctl(fd, 0x40044DA2u, &password) >= 0;
}

bool xm_spiflash_setpasswd(int fd, int password) {
    assert(fd);
    // XMMTD_SETPASSWD
    return ioctl(fd, 0x40044DA1u, &password) >= 0;
}

bool xm_spiflash_unlock_user(int fd, int data) {
    assert(fd);
    // XMMTD_UNLOCKUSER
    return ioctl(fd, 0x40044DA8u, &data) >= 0;
}

static int xm_spiflash_getlockversion(int fd) {
    assert(fd);
    int num;
    // XMMTD_GETLOCKVERSION
    if (ioctl(fd, 0x40044DA0u, &num) >= 0) {
        return num;
    }
    return 0;
}

int xm_spiflash_getprotectflag(int fd) {
    assert(fd);
    int num;
    // XMMTD_GETPROTECTFLAG
    if (ioctl(fd, 0x40044dbf, &num) >= 0) {
        return num;
    }
    return -1;
}

typedef struct {
    uint32_t offset;
    uint32_t size;
} __attribute__((packed)) XmFlashIO;

static bool xm_flash_op(int fd, uint32_t op, uint32_t offset, uint32_t size) {
    assert(fd);
    XmFlashIO io;
    io.offset = offset;
    io.size = size;
    return ioctl(fd, op, &io) >= 0;
}

static int pwd;

bool xm_spiflash_unlock_and_erase(int fd, uint32_t offset, uint32_t size) {
    xm_spiflash_checkpasswd(fd, pwd);
    if (!xm_flash_op(fd, 0x40084da6, offset, size))
        return false;
    if (!xm_flash_op(fd, 0x40084d02, offset, size))
        return false;
    return true;
}

static int xm_randompasswd() {
    time_t t = time(0);
    srand(t);
    return rand();
}

static bool xm_flash_start(int fd) {
    pwd = xm_randompasswd();
    bool lock_supported = xm_spiflash_getlockversion(fd);
    if (lock_supported) {
        if (!xm_spiflash_setpasswd(fd, pwd))
            return false;
        int pflag = xm_spiflash_getprotectflag(fd);
        if (pflag > 0) {
            if (pflag == 1) {
                // printf("Flag is 1\n");
            }
        } else {
            // TODO: other cases
            fprintf(stderr, "Not implemented yet\n");
            return false;
        }
        xm_spiflash_checkpasswd(fd, pwd);
        if (!xm_spiflash_unlock_user(fd, 0))
            return false;
        xm_spiflash_checkpasswd(fd, 0);
    }
    return true;
}

bool xm_flash_init(int fd) {
    if (!xm_flash_start(fd))
        return false;
    return true;
}

static bool detect_xm_product() {
    char buf[256];

    if (!get_regex_line_from_file("/usr/bin/ProductDefinition",
                                  "\"Hardware\" : \"(.+)\"", buf,
                                  sizeof(buf))) {
        return false;
    }
    strcpy(board_id, buf);
    return true;
}

static bool extract_cloud_id() {
    char buf[256];

    if (!get_regex_line_from_file("/mnt/mtd/Config/SerialNumber", "([0-9a-f]+)",
                                  buf, sizeof(buf))) {
        return false;
    }
    sprintf(board_specific + strlen(board_specific), "  cloudId: %s\n", buf);
    return true;
}

static void extract_netip_creds(char username[64], char pwd[64]) {
    FILE *fp = fopen("/mnt/mtd/Config/Account1", "r");
    if (!fp)
        return;

    fseek(fp, 0, SEEK_END);
    size_t len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *config = malloc(len);
    fread(config, 1, len, fp);
    fclose(fp);
    cJSON *json = cJSON_ParseWithLength(config, len);
    if (json == NULL)
        goto bailout;

    const cJSON *users = cJSON_GetObjectItemCaseSensitive(json, "Users");
    if (!cJSON_IsArray(users))
        goto bailout;
    const cJSON *user = NULL;
    cJSON_ArrayForEach(user, users) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(user, "Name");
        if (!(cJSON_IsString(name) && (name->valuestring != NULL)))
            goto bailout;
        const cJSON *password =
            cJSON_GetObjectItemCaseSensitive(user, "Password");
        if (!(cJSON_IsString(password) && (password->valuestring != NULL)))
            goto bailout;
        strncpy(username, name->valuestring, 64);
        strncpy(pwd, password->valuestring, 64);
        break;
    }

bailout:
    free(config);
    cJSON_Delete(json);
}

void gather_xm_board_info() {
    char username[64] = {0}, password[64] = {0};
    extract_netip_creds(username, password);
    // printf("%s/%s\n", username, password);

    detect_xm_product();
    extract_cloud_id();
    detect_nor_chip();
}

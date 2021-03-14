#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "cjson/cJSON.h"

#include "chipid.h"
#include "firmware.h"
#include "hisi/hal_hisi.h"
#include "tools.h"
#include "vendors/xm.h"

bool is_xm_board() {
    // crucial to keep detection status in memory for deal with XM flash ops
    static bool detected;
    if (detected)
        return detected;

    if (!access("/mnt/mtd/Config/Account1", 0) ||
        !access("/proc/xm/xminfo", 0)) {
        strcpy(board_manufacturer, "Xiongmai");
        detected = true;
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

static bool xm_spiflash_checkpasswd(int fd, int password) {
    assert(fd);
    // XMMTD_CHECKPASSWD
    return ioctl(fd, 0x40044DA2u, &password) >= 0;
}

static bool xm_spiflash_setpasswd(int fd, int password) {
    assert(fd);
    // XMMTD_SETPASSWD
    return ioctl(fd, 0x40044DA1u, &password) >= 0;
}

static bool xm_spiflash_unlock_user(int fd, int data) {
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

static int xm_spiflash_getprotectflag(int fd) {
    assert(fd);
    int num;
    // XMMTD_GETPROTECTFLAG
    if (ioctl(fd, 0x40044dbf, &num) >= 0) {
        return num;
    }
    return -1;
}

static bool xm_spiflash_protectdisabled(int fd, int num) {
    assert(fd);
    // XMMTD_PROTECTDISABLED
    return ioctl(fd, 0x40044DBDu, &num) >= 0;
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

    // may fail on XmEnv based boards but then burn will be ok
    xm_flash_op(fd, 0x40084da6, offset, size);

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
            xm_spiflash_checkpasswd(fd, pwd);
            xm_spiflash_protectdisabled(fd, 0);
            xm_spiflash_checkpasswd(fd, 0);
        }
        xm_spiflash_checkpasswd(fd, pwd);

        // may fail on XmEnv based boards but then burn will be ok
        xm_spiflash_unlock_user(fd, 0);

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

    if (!get_regex_line_from_file("/mnt/custom/ProductDefinition",
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

static bool extract_snsType() {
    char buf[256];

    if (!get_regex_line_from_file("/mnt/mtd/Config/SensorType.bat",
                                  "snsType:([0-9]+)", buf, sizeof(buf))) {
        return false;
    }
    sprintf(board_specific + strlen(board_specific), "  snsType: %s\n", buf);
    return true;
}

static void extract_netip_creds(char username[64], char pwd[64]) {
    size_t len;
    char *config = file_to_buf("/mnt/mtd/Config/Account1", &len);
    if (!config)
        goto bailout;

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
    if (json)
        cJSON_Delete(json);
}

void gather_xm_board_info() {
    char username[64] = {0}, password[64] = {0};
    extract_netip_creds(username, password);
    // printf("%s/%s\n", username, password);

    detect_xm_product();
    extract_cloud_id();
    extract_snsType();
    detect_nor_chip();
}

static uint32_t CV200_WDG_CONTROL = 0x20040000 + 0x0008;
static uint32_t CV300_WDG_CONTROL = 0x12080000 + 0x0008;
static uint32_t EV300_WDG_CONTROL = 0x12030000 + 0x0008;

static bool xm_disable_watchdog() {
    getchipid();
    uint32_t zero = 0;
    int ret = 0;
    switch (chip_generation) {
    case HISI_V1:
    case HISI_V2:
        mem_reg(CV200_WDG_CONTROL, &zero, OP_WRITE);
        break;
    case HISI_V3:
        ret = delete_module("xm_watchdog", 0);
        mem_reg(CV300_WDG_CONTROL, &zero, OP_WRITE);
        break;
    case HISI_V4:
        ret = delete_module("hi3516ev200_wdt", 0);
        mem_reg(EV300_WDG_CONTROL, &zero, OP_WRITE);
        break;
    default:
        return false;
    }
    if (ret == -1) {
        fprintf(stderr, "delete_module, errno: %s\n", strerror(errno));
        return false;
    }
    return true;
}

bool xm_kill_stuff(bool force) {
    char proc[255] = {0};
    pid_t gpid = get_god_pid(proc, sizeof(proc));
    if (strcmp(proc, "Sofia")) {
        fprintf(stderr, "There was no Sofia process detected\n");
        if (!force) {
            printf("Use --force switch to skip the check\n");
            return false;
        }
    } else {
        kill(gpid, SIGINT);
        printf("Sofia has been terminated, waiting for watchdog...\n");
        sleep(5);
        if (!xm_disable_watchdog()) {
            fprintf(stderr, "Cannot disarm watchdog\n");
            return false;
        }

        int downcount = 2 * 60;
        printf("Ensuring everything shutdown\n");
        for (int i = downcount; i > 0; i--) {
            printf("%d seconds %5s\r", i, "");
            fflush(stdout);
            sleep(1);
        }
        printf("Done %32s\n", "");
    }

    return true;
}

#include <stdbool.h>
#include <stdio.h>

#include <unistd.h>

#include "chipid.h"
#include "cjson/cYAML.h"
#include "ethernet.h"
#include "hal_hisi.h"
#include "network.h"
#include "tools.h"

struct REG_MDIO_RWCTRL {
    unsigned int phy_inaddr : 5;
    unsigned int frq_dv : 3;
    unsigned int phy_exaddr : 5;
    unsigned int rw : 1;
    unsigned int res : 1;
    bool finish : 1;
    unsigned int cpu_data_in : 16;
};

#define MDIO_RWCTRL 0x1100
#define MDIO_RO_DATA 0x1104
#define U_MDIO_PHYADDR 0x0108
#define D_MDIO_PHYADDR 0x2108
#define U_MDIO_RO_STAT 0x010C
#define D_MDIO_RO_STAT 0x210C

uint32_t hieth_readl(uint32_t base, uint32_t regaddr) {
    uint32_t val;
    if (mem_reg(base + regaddr, &val, OP_READ)) {
        return val;
    }
    return 0x1111;
}

void hieth_writel(uint32_t val, uint32_t base, uint32_t regaddr) {
    if (!mem_reg(base + regaddr, &val, OP_WRITE)) {
        fprintf(stderr, "write error\n");
    }
}

/* hardware set bit'15 of MDIO_REG(0) if mdio ready */
#define test_mdio_ready(base) (hieth_readl(base, MDIO_RWCTRL) & (1 << 15))

static int wait_mdio_ready(uint32_t base) {
    int timeout_us = 1000;
    while (--timeout_us && !test_mdio_ready(base))
        usleep(1);
    return timeout_us;
}

#define MDIO_MK_RWCTL(cpu_data_in, finish, rw, phy_exaddr, frq_div,            \
                      phy_regnum)                                              \
    (((cpu_data_in) << 16) | (((finish)&0x01) << 15) | (((rw)&0x01) << 13) |   \
     (((phy_exaddr)&0x1F) << 8) | (((frq_div)&0x7) << 5) |                     \
     ((phy_regnum)&0x1F))

#define mdio_start_phyread(base, frq_dv, phy_addr, regnum)                     \
    hieth_writel(MDIO_MK_RWCTL(0, 0, 0, phy_addr, frq_dv, regnum), base,       \
                 MDIO_RWCTRL)

#define mdio_get_phyread_val(base) (hieth_readl(base, MDIO_RO_DATA) & 0xFFFF)

int hieth_mdio_read(int frq_dv, int phy_addr, uint32_t base, int regnum) {
    int val = 0;

    if (!wait_mdio_ready(base)) {
        fprintf(stderr, "mdio busy\n");
        goto error_exit;
    }

    mdio_start_phyread(base, frq_dv, phy_addr, regnum);

    if (wait_mdio_ready(base))
        val = mdio_get_phyread_val(base);
    else
        fprintf(stderr, "read timeout\n");

error_exit:
#if 0
    fprintf(stderr, "phy_addr = %d, regnum = %d, val = 0x%04x\n", phy_addr,
            regnum, val);
#endif

    return val;
}

cJSON *detect_ethernet() {
    cJSON *fake_root = cJSON_CreateObject();
    cJSON *j_inner = cJSON_CreateObject();
    cJSON_AddItemToObject(fake_root, "ethernet", j_inner);

    char mac[20];

    if (get_mac_address(mac, sizeof mac)) {
        ADD_PARAM("mac", mac);
    };

    uint32_t mdio_base = 0;
    switch (chip_generation) {
    case HISI_V1:
    case HISI_V2:
        mdio_base = 0x10090000;
        break;
    case HISI_V3:
        mdio_base = 0x10050000;
        break;
    }

    if (mdio_base) {
        struct REG_MDIO_RWCTRL reg;
        if (mem_reg(mdio_base + MDIO_RWCTRL, (uint32_t *)&reg, OP_READ)) {
            uint32_t my_phyaddr = hieth_readl(mdio_base, U_MDIO_PHYADDR);
            ADD_PARAM_FMT("u-mdio-phyaddr", "%d", my_phyaddr);

            unsigned long phy_id;
            unsigned short id1, id2;
            id1 = hieth_mdio_read(reg.frq_dv, my_phyaddr, mdio_base, 0x02);
            id2 = hieth_mdio_read(reg.frq_dv, my_phyaddr, mdio_base, 0x03);
            phy_id = (((id1 & 0xffff) << 16) | (id2 & 0xffff));
            ADD_PARAM_FMT("phy-id", "0x%.8lx", phy_id);
            ADD_PARAM_FMT("d-mdio-phyaddr", "%x",
                          hieth_readl(mdio_base, D_MDIO_PHYADDR));
        }

        if (chip_generation == HISI_V1)
            ADD_PARAM("phy-mode", hisi_cv100_get_mii_mux());
    }
    return fake_root;
}

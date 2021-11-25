# IP camera hardware checking tool

[![Build](https://github.com/OpenIPC/ipctool/actions/workflows/release-it.yml/badge.svg)](https://github.com/OpenIPC/ipctool/actions/workflows/release-it.yml)
[![CI status](https://img.shields.io/github/downloads/OpenIPC/ipctool/total.svg)](https://github.com/OpenIPC/ipctool/releases)
![GitHub repo size](https://img.shields.io/github/repo-size/OpenIPC/ipctool)
![GitHub issues](https://img.shields.io/github/issues/OpenIPC/ipctool)
![GitHub pull requests](https://img.shields.io/github/issues-pr/OpenIPC/ipctool)
[![License](https://img.shields.io/github/license/OpenIPC/ipctool)](https://opensource.org/licenses/MIT)

This basic concept belongs to [Maxim Chertov](http://github.com/chertov) (thank you for
your original utility) and [Nikita Orlov](https://github.com/nikitos1550/) (for
cute YAML format for describing hardware). A warm welcome also to [Igor
Zalatov](https://github.com/ZigFisher) (for suggestions for new features and
describing ways to do them).

-----

### Supporting

If you like our work, please consider supporting us on [Opencollective](https://opencollective.com/openipc/contribute/backer-14335/checkout) or [PayPal](https://www.paypal.com/donate/?hosted_button_id=C6F7UJLA58MBS) or [YooMoney](https://openipc.org/donation/yoomoney.html). 

[![Backers](https://opencollective.com/openipc/tiers/backer/badge.svg?label=backer&color=brightgreen)](https://opencollective.com/openipc)
[![Backers](https://opencollective.com/openipc/tiers/badge.svg)](https://opencollective.com/openipc)

[![Backers](https://opencollective.com/openipc/tiers/backer.svg?avatarHeight=36)](https://opencollective.com/openipc#support)

### Thanks a lot !!!

<p align="center">
<a href="https://opencollective.com/openipc/contribute/backer-14335/checkout" target="_blank"><img src="https://opencollective.com/webpack/donate/button@2x.png?color=blue" width="300" alt="OpenCollective donate button" /></a>
<a href="https://www.paypal.com/donate/?hosted_button_id=C6F7UJLA58MBS"><img src="https://www.paypalobjects.com/en_US/IT/i/btn/btn_donateCC_LG.gif" alt="PayPal donate button" /> </a>
<a href="https://openipc.org/donation/yoomoney.html"><img src="https://yoomoney.ru/transfer/balance-informer/balance?id=596194605&key=291C29A811B500D7" width="100" alt="YooMoney donate button" /> </a>
</p>

-----

## Download

Use [the
link](https://github.com/OpenIPC/ipctool/releases/download/latest/ipctool)
to download latest build (even directly to your camera). The build uses `musl`
C library to work on vast majority of hardware.

### Alternative launch methods

* *Public NFS server* (in case your camera firmware includes NFS client
    support, proven to work on XM cameras):

    ```console
    $ mount -o nolock 95.217.179.189:/srv/ro /utils/
    $ /utils/ipctool
    ```

* *Using telnet/console and uget utility*: basically convert small `uget` binary
  into `echo`/`printf` chunks and deploy to `/tmp` partition. Read
  [more in documentation](https://github.com/widgetii/uget)

## Usage

```console
# ipctool
    available options:

	--chip_id
	--sensor_id
	--temp

	--dmesg
	--printenv
	--setenv key=value

	[--skip-env] [--force] --restore
	--help

    (or run without params to get full available information)
```

When run without parameters utility produces YAML with all hardware-specific
information about given IP-camera or DVR:

```yaml
---
board:
  vendor: Xiongmai
  model: 50H20L
  cloudId: 3beae2b40d84f889
chip:
  vendor: HiSilicon
  model: 3516CV300
ethernet:
  mac: "00:12:89:12:88:e1"
  u-mdio-phyaddr: 1
  phy-id: 0x001cc816
  d-mdio-phyaddr: 0
rom:
  - type: nor
    block: 64K
    chip:
      name: "w25q128"
      id: 0xef4018
    partitions:
      - name: boot
        size: 0x30000
        sha1: 7a7a83e9
        contains:
          - name: xmcrypto
            offset: 0x1fc00
          - name: uboot-env
            offset: 0x20000
      - name: romfs
        size: 0x2e0000
        path: /,squashfs
        sha1: 62529dab
      - name: user
        size: 0x300000
        path: /usr,squashfs
        sha1: cbb7e9ca
      - name: web
        size: 0x160000
        path: /mnt/custom/data/Fonts,squashfs
        sha1: 48140b3b
      - name: custom
        size: 0x40000
        path: /mnt/custom,cramfs
        sha1: fb72a5f5
      - name: mtd
        size: 0x50000
        path: /mnt/mtd,jffs2,rw
    size: 8M
    addr-mode: 3-byte
ram:
  total: 128M
  media: 72M
firmware:
  u-boot: "2010.06-svn1098 (Jun 11 2018 - 13:17:42)"
  kernel: "3.18.20 (Thu Jul 5 14:44:19 CST 2018)"
  toolchain: gcc version 4.9.4 20150629 (prerelease) (Hisilicon_v500_20170922) 
  libc: uClibc 0.9.33.2
  sdk: "Hi3516CV300_MPP_V1.0.0.0 B010 Release (Jun 22 2018, 19:22:22)"
  god-app: /usr/bin/Sofia
sensors:
- vendor: Sony
  model: IMX291
  control:
    bus: 0
    type: i2c
    addr: 0x34
  params:
    bitness: 12
    databus: LVDS 4 ch
    fps: 30
  data:
    type: LVDS
    lane-id:
    - 0
    - 1
    - 2
    - 3
    lvds-wdr-en: 0
    lvds-wdr-mode: 0
    lvds-wdr-num: 0
    raw-data-type: RAW_DATA_12BIT
    sync-mode: LVDS_SYNC_MODE_SAV
    data-endian: LVDS_ENDIAN_BIG
    sync-code-endian: LVDS_ENDIAN_BIG
    sync-code:
    - 
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
    - 
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
    - 
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
    - 
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
      - 0xab0, 0xb60, 0x800, 0x9d0
  clock: 37.125MHz
```

### In your own scripts

* Determine chip id:

    ```console
    # ipctool --chip_id
    hi3516cv300
    ```

* Determine sensor model and control line:

    ```console
    # ipctool --sensor_id
    imx291_i2c
    ```

* Get temperature from chip's internal sensor (not all devices supported):

    ```console
    # ipctool --temp
    50.69
    ```

### As backup/restore tool

* Save full backup with YAML metadata into specific file:

    ```console
    # mount -o nolock mynfsserver:/srv /var/utils
    # ipctool --backup /var/utils/mybackup-00:12:17:83:d6:39
    # sync
    ```

### As reverse engineering tool

* Drop-in replacement of `dmesg` command:

    ```console
    # ipctool --dmesg
    ```

* Drop-in replacement of `fw_printenv` and `fw_setenv` commands:

    ```console
    # ipctool --printenv | grep bootargs
    # ipctool --setenv bootargs="mem=\${osmem} mtdparts=hi_sfc:256k(boot),64k(env),2048k(kernel),5120k(rootfs),-(rootfs_data)"
    ```

## Supported SoCs

Tested on:

|Manufacturer|Models|
|---|---|
|[HiSilicon](https://github.com/openIPC/camerasrnd/#chip-families-information)|Hi3516CV100/200/300, Hi3516EV100/200/300, Hi3516DV300|
|[SigmaStar](http://linux-chenxing.org/)|SSC335|
|[Xiongmai](http://www.xiongmaitech.com/product)|XM510, XM530, XM550|

Please test on your device to help us extend the list.

## Supported boards

Tested on:

|Manufacturer|Models|
|---|---|
|Xiongmai| Various models |
|Hankvision | V6202IR-IMX327 |
|Ruision | RS-H649F-A0, RS-H651JAI-AO, RS-H656S-AO |

## Supported sensors

Tested on:

|Manufacturer           |Models                                 |
|-----------------------|---------------------------------------|
|Silicon Optronics, Inc.|JX-F22, JX-F23, JX-F37, JX-H62, JX-H65, JX-K05 |
|Sony                   |IMX224, IMX290, IMX291, IMX307, IMX322, IMX323, IMX327, IMX335, IMX415 |
|ON Semiconductor       |AR0130, AR0237                         |
|SmartSens              |SC2135, SC2232, SC2235, SC2235P, SC2239, SC2315e (SC307E, SC4239Р), SC335E (SC5300) |
|OmniVision             |OV9712                                 |
|GalaxyCore             |GC2053                                 |

Please test on your device to help us extend the list.

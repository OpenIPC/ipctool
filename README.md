![OpenIPC Logo](https://cdn.themactep.com/images/logo_openipc.png)

# IP camera hardware checking tool

[![Build](https://github.com/OpenIPC/ipctool/actions/workflows/release-arm32.yml/badge.svg)](https://github.com/OpenIPC/ipctool/actions/workflows/release-arm32.yml)
[![Build](https://github.com/OpenIPC/ipctool/actions/workflows/release-mips32.yml/badge.svg)](https://github.com/OpenIPC/ipctool/actions/workflows/release-mips32.yml)
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

## Download

Use [the link](https://github.com/OpenIPC/ipctool/releases/download/latest/ipctool)
to download latest build (even directly to your camera). The build uses `musl`
C library to work on vast majority of hardware.

### Alternative launch methods

* *Your own NFS server* (in case your camera firmware includes NFS client
    support, proven to work on XM cameras):

    ```console
    $ mount -o nolock 10.0.0.1:/srv/ro /utils/
    $ /utils/ipctool
    ```

   As an alternative, you may run your own NFS server, putting ipctool on it.

* *Using UART and rx busybox applet on camera side*. This option was described in [@themactep blog post](https://themactep.com/notes/how-to-upload-a-file-onto-ip-camera-via-serial-uart-conection).

* *Using telnet/console and uget utility*: basically convert small `uget` binary
  into `echo`/`printf` chunks and deploy to `/tmp` partition. Read
  [more in documentation](https://github.com/widgetii/uget)

* *TFTP*, since some cameras have tftp clients and/or servers by default. Assuming
  you have the `ipctool-mips32` binary ready under `/directory/to/serve`:

  **On a desktop computer:**
  ```
  $ pip install ptftpd
  $ ptftpd -p 6969 en0 /directory/to/serve
  ```
  **On the camera:**
  ```
   # tftp -r /directory/to/serve/ipctool-mips32 -g 192.168.1.107 6969

     46% |**************                 | 61952   0:00:01 ETA
  ```

* *Using telnet/console only*: uses a python script to transfer ipctool via
  telnet/echo to the camera.

  **On a desktop computer:**
  ```
  $ tools/telnet_upload.py 192.168.1.10
  ```
  **On the shell:**
  ```
  # transfer
  ```

## Usage

```console
# ipctool -h
Usage: ipctool [OPTIONS] [COMMANDS]
Where:
  -c, --chip-name           read chip name
  -s, --sensor-name         read sensor model and control line
  -t, --temp                read chip temperature (where supported)

  backup <filename>         save backup into a file
  upload                    upload full backup to the OpenIPC cloud
  restore [mac|filename]    restore from backup (cloud-based or local file)
     [-s, --skip-env]       skip environment
     [-f, --force]          enforce
  upgrade <bundle>          upgrade to OpenIPC firmware
                            (experimental! use only on cameras with UART)
     [-f, --force]          enforce
  printenv                  drop-in replacement for fw_printenv
  setenv <key> <value>      drop-in replacement for fw_setenv
  dmesg                     drop-in replacement for dmesg
  i2cget <device address> <register>
  spiget <register>
                            read data from I2C/SPI device
  i2cset <device address> <register> <new value>
  spiset <register> <new value>
                            write a value to I2C/SPI device
  i2cdump [--script] [-b, --bus] <device address> <from register> <to register>
  spidump [--script] <from register> <to register>
                            dump data from I2C/SPI device
  i2cdetect [-b, --bus]     attempt to detect devices on I2C bus
  reginfo [--script]        dump current status of pinmux registers
  gpio (scan|mux)           GPIO utilities
  trace [--skip=usleep] <full/path/to/executable> [program arguments]
                            dump original firmware calls and data structures
  -h, --help                this help
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
  main-app: /usr/bin/Sofia
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

* Determine chip name:

    ```console
    # ipctool --chip-name
    hi3516cv300
    ```

* Determine sensor model and control line:

    ```console
    # ipctool --sensor-name
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
    # ipctool backup /var/utils/mybackup-00:12:17:83:d6:39
    # sync
    ```

### As reverse engineering tool

* Drop-in replacement of `dmesg` command:

    ```console
    # ipctool dmesg
    ```

* Drop-in replacement of `fw_printenv` and `fw_setenv` commands:

    ```console
    # ipctool printenv | grep bootargs
    # ipctool setenv bootargs "mem=\${osmem} mtdparts=hi_sfc:256k(boot),64k(env),2048k(kernel),5120k(rootfs),-(rootfs_data)"
    ```

* Drop-in replacement of `i2cget`, `i2cset` and `i2cdump` commands from
  `i2c-tools` package:

    ```console
    # ipctool i2cget 0x34 0x3000
    # ipctool i2cset 0x34 0x3000 1
    # ipctool i2cdump 0x34 0x3000 0x31ff
    # ipctool i2cdump --script 0x34 0x3000 0x31ff
    ```

* The same approach is to manipulate SPI sensor registers:

    ```console
    # ipctool spiget 0x200
    # ipctool spiset 0x200 1
    # ipctool spidump 0x200 0x300
    # ipctool spidump --script 0x200 0x300
    ```

* Dump the state of pinmux registers in human- and machine-readable format or
  shell script ready to be applied on another system:

    ```console
    # ipctool reginfo
    # ipctool --script reginfo
    ```

* Advanced replacement of `strace`:

    ```console
    # ipctool trace /usr/bin/Sofia
    ```

### To help the researcher

On Ingenic devices, the original Sensor I2C address needs to be right shifted by 1bit, example:

```
IMX335: (0x34 >> 1) = 0x1A
SC2230: (0x60 >> 1) = 0x30
GC2053: (0x6E >> 1) = 0x37
```

## Supported SoCs

Tested on:

|Manufacturer|Models|
|---|---|
|[HiSilicon](https://github.com/openIPC/camerasrnd/#chip-families-information)|Hi3516CV100/200/300, Hi3516EV100/200/300, Hi3516DV300, Hi3518EV100|
|[SigmaStar](http://linux-chenxing.org/)|SSC335|
|[Xiongmai](http://www.xiongmaitech.com/product)|XM510, XM530, XM550|
|[Rockchip](https://www.rock-chips.com/)|RV1109|
|[Goke](http://www.goke.com/en/)|GK7205v200/210/300|

Please test on your device to help us extend the list.

## Supported boards

Tested on:

|Manufacturer|Models|
|---|---|
|Xiongmai| Various models |
|Hankvision | V6202IR-IMX327 |
|Ruision | RS-H649F-A0, RS-H651JAI-AO, RS-H656S-AO |
|TP-Link | NC210 |

## Supported sensors

Tested on:

|Manufacturer           |Models                                 |
|-----------------------|---------------------------------------|
|Silicon Optronics, Inc.|JX-F22, JX-F23, JX-F37, JX-H62, JX-H65, JX-K05 |
|Sony                   |IMX224, IMX290, IMX291, IMX307, IMX322, IMX323, IMX327, IMX335, IMX415, IMX664 |
|ON Semiconductor       |AR0130, AR0237                         |
|SmartSens              |SC2135, SC223A, SC2232, SC2235, SC2235P, SC2239, SC2315e (SC307E, SC4239Р), SC335E (SC5300) |
|OmniVision             |OV9712                                 |
|GalaxyCore             |GC2053                                 |

Please test on your device to help us extend the list.

-----

### Support

OpenIPC offers two levels of support.

- Free support through the community (via [chat](https://openipc.org/#telegram-chat-groups)).
- Paid commercial support (from the team of developers).

Please consider subscribing for paid commercial support if you intend to use our product for business.
As a paid customer, you will get technical support and maintenance services directly from our skilled team.
Your bug reports and feature requests will get prioritized attention and expedited solutions. It's a win-win
strategy for both parties, that would contribute to the stability your business, and help core developers
to work on the project full-time.

If you have any specific questions concerning our project, feel free to [contact us](mailto:dev@openipc.org).

### Participating and Contribution

If you like what we do, and willing to intensify the development, please consider participating.

You can improve existing code and send us patches. You can add new features missing from our code.

You can help us to write a better documentation, proofread and correct our websites.

You can just donate some money to cover the cost of development and long-term maintaining of what we believe
is going to be the most stable, flexible, and open IP Network Camera Framework for users like yourself.

You can make a financial contribution to the project at [Open Collective](https://opencollective.com/openipc/contribute/backer-14335/checkout).

Thank you.

<p align="center">
<a href="https://opencollective.com/openipc/contribute/backer-14335/checkout" target="_blank"><img src="https://opencollective.com/webpack/donate/button@2x.png?color=blue" width="375" alt="Open Collective donate button"></a>
</p>

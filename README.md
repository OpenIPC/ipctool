# IPC chip information

[![ipctool-build](https://github.com/OpenIPC/ipctool/actions/workflows/release-it.yml/badge.svg)](https://github.com/OpenIPC/ipctool/actions/workflows/release-it.yml)

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
chip:
  vendor: HiSilicon
  model: 3516CV200
ethernet:
  mac: "00:12:17:83:d6:39"
rom:
  - type: nor
    size: 8M
    block: 64K
    partitions:
      - name: boot
        size: 0x50000
      - name: kernel
        size: 0x380000
      - name: rootfs
        size: 0x150000
      - name: rootfs_data
        size: 0x2e0000
sensors:
  - vendor: Sony
    model: IMX323
    control:
      bus: 0
      type: i2c
    data:
      type: DC
    clock: 24MHz
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
|[HiSilicon](https://github.com/openIPC/camerasrnd/#chip-families-information)|Hi3516CV100/200/300, Hi3516EV100/200/300|
|[SigmaStar](http://linux-chenxing.org/)|SSC335|
|[Xiongmai](http://www.xiongmaitech.com/product)|XM510, XM530, XM550|

Please test on your device to help us extend the list.

## Supported boards

Tested on:

|Manufacturer|Models|
|---|---|
|Xiongmai| Various models |
|Hankvision | V6202IR-IMX327 |
|Ruision | RS-H649F-A0 |

## Supported sensors

Tested on:

|Manufacturer           |Models                                 |
|-----------------------|---------------------------------------|
|Silicon Optronics, Inc.|JX-F22, JX-F23, JX-F37, JX-H62, JX-H65, JX-K05 |
|Sony                   |IMX291, IMX307, IMX322, IMX323, IMX327, IMX335 |
|ON Semiconductor       |AR0130, AR0237                         |
|SmartSens              |SC2135, SC2235, SC2235P, SC307E, SC335E |
|OmniVision             |OV9712                                 |

Please test on your device to help us extend the list.

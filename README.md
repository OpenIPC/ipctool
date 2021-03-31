# IPC chip information

![ipc-chip-info](https://github.com/OpenIPC/ipctool/workflows/ipc-chip-info/badge.svg)

Thes basic concept belongs to [@cherov](http://github.com/chertov)(thank you for
your original utility) and [@nikitos1550](https://github.com/nikitos1550/)(for
cute YAML format for describing hardware). A warm welcome also to [Igor
Zalatov](https://github.com/ZigFisher)(for suggestions for new features and
describing ways to do them).

## Download

Use [the
link](https://github.com/OpenIPC/ipctool/releases/download/latest/ipctool)
to download latest build (even directly to your camera). The build use `musl` to
work on vast majority of hardware.

## Usage

```
# ./ipctool --help
    available options:
        --system_id
        --chip_id
        --sensor_id
        --temp
        --dmesg
        --printenv
        --setenv
        --wait
        [--skip-env] --restore
        --help

    (or run without params to get full available information)
```

When run without parameters utility produces YAML with all hardware-specific
information about given IPC:

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

    ```
    root@IVG-HP203Y-AE# ipctool --chip_id
    hi3516cv300
    ```

* Determine sensor model and control line:

    ```
    root@IVG-HP203Y-AE# ipctool --sensor_id
    imx291_i2c
    ```

* Get temperature from chip's internal sensor (not all devices supported):

    ```
    root@IVG-HP203Y-AE:~# /utils/sdk/ipctool --temp
    50.69
    ```

* Drop-in replacement of `dmesg` command:

    ```
    root@IVG-HP203Y-AE:~# /utils/sdk/ipctool --dmesg
    ```

## Supported SoCs

Tested on:

|Manufacturer|Models|
|---|---|
|[HiSilicon](https://github.com/openIPC/camerasrnd/#chip-families-information)|hi3516cv100/200/300, hi3516ev100/200/300|
|Xiongmai|XM510, XM530|

Please test on your device to help us extend the list.

## Supported sensors

Tested on:

|Manufacturer           |Models                                 |
|-----------------------|---------------------------------------|
|Silicon Optronics, Inc.|JX-F22, JX-F23, JX-F37, JX-H62, JX-H65 |
|Sony                   |IMX291, IMX307, IMX322, IMX323, IMX335 |
|ON Semiconductor       |AR0130, AR0237                         |
|SmartSens              |SC2135, SC2235, SC2235P, SC5300        |
|OmniVision             |OV9712                                 |

Please test on your device to help us extend the list.

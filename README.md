# IPC chip information

![ipc-chip-info](https://github.com/OpenIPC/ipc_chip_info/workflows/ipc-chip-info/badge.svg)

## Download

Use [the
link](https://github.com/OpenIPC/ipc_chip_info/releases/download/latest/ipc_chip_info)
to download latest build (even directly to your camera). The build use `musl` to
work on vast majority of hardware.

## Usage

```
# ./ipc_chip_info --help
    available options:
        --system_id
        --chip_id
        --sensor_id
        --isp_register
        --isp_version
        --isp_build
        --isp_sequence_number
        --mpp_info
        --temp
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
    root@IVG-HP203Y-AE# ipc_chip_info --chip_id
    hi3516cv300
    ```

* Determine sensor model and control line:

    ```
    root@IVG-HP203Y-AE# ipc_chip_info --sensor_id
    imx291_i2c
    ```

* Determine HiSilicon SDK internal parameters in runtime:

    ```
    root@IVG-HP203Y-AE# eval $(ipc_chip_info --mpp_info) env | grep HI_
    HI_CHIPID=0X3516C300
    HI_VERSION=Hi3516CV300_MPP_V1.0.4.0 B050 Release
    ```

* Get temperature from chip's internal sensor (not all devices supported):

    ```
    root@IVG-HP203Y-AE:~# /utils/sdk/ipc_chip_info --temp
    50.69
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
|Sony                   |IMX291, IMX307, IMX323, IMX335         |
|ON Semiconductor       |AR0130, AR0237                         |
|SmartSens              |SC2235, SC2235P, SC5300                |

Please test on your device to help us extend the list.

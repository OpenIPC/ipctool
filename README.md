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
        --mpp_version
        --help

    (or run without params to get full available information)
```

## Supported SoCs

Tested on:

|Manufacturer|Models|
|---|---|
|[HiSilicon](https://github.com/openIPC/camerasrnd/#chip-families-information)|hi3516cv300, hi3516ev200, hi3516ev300|
|Xiongmai|XM510, XM530|

Please test on your device to help us extend the list.

## Supported sensors

Tested on:

|Manufacturer|Models|
|---|---|
|Silicon Optronics, Inc.|JX-F22, JX-F23, JX-F37, JX-H62, JX-H65 |
|Sony|IMX291, IMX307, IMX323, IMX335|
|ON Semiconductor|AR0130|
|SmartSens|SC2235, SC2235P, SC5300|

Please test on your device to help us extend the list.

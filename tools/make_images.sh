#!/usr/bin/env bash

PACK=--pack

# EV300
wget -qO-  \
  https://github.com/OpenIPC/firmware/releases/download/latest/openipc.hi3516ev300-br.tgz \
  | tar xvfz - -C /tmp
./upgrade_bundle.py \
  --kernel /tmp/uImage.hi3516ev300 \
  --rootfs /tmp/rootfs.squashfs.hi3516ev300 \
  -o upgrade.ev300 $PACK \
  -i \
  --cma 'mmz=anonymous,0,0x42000000,96M mmz_allocator=cma'

# CV300
wget -P /tmp \
  https://github.com/OpenIPC/chaos_calmer/releases/download/latest/openwrt-hi35xx-16cv300-u-boot.bin
wget -P /tmp \
  https://github.com/OpenIPC/chaos_calmer/releases/download/latest/openwrt-hi35xx-16cv300-default-uImage
wget -P /tmp \
  https://github.com/OpenIPC/chaos_calmer/releases/download/latest/openwrt-hi35xx-16cv300-default-root.squashfs
./upgrade_bundle.py \
  --boot /tmp/openwrt-hi35xx-16cv300-u-boot.bin \
  --kernel /tmp/openwrt-hi35xx-16cv300-default-uImage \
  --rootfs /tmp/openwrt-hi35xx-16cv300-default-root.squashfs \
  -o upgrade.cv300 $PACK

#!/usr/bin/env bash

PACK=--pack

wget -qO-  \
  https://github.com/OpenIPC/openipc-2.1/releases/download/latest/openipc.hi3516ev300-br.tgz \
  | tar xvfz - -C /tmp
./upgrade_bundle.py \
  --kernel /tmp/uImage.hi3516ev300 \
  --rootfs /tmp/rootfs.squashfs.hi3516ev300 \
  -o upgrade.ev300 $PACK \
  -i \
  --cma 'mmz=anonymous,0,0x42000000,96M mmz_allocator=cma'

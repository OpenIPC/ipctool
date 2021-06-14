#!/usr/bin/env bash

./upgrade_bundle.py --kernel /mnt/noc/uImage.hi3516ev300 --rootfs /mnt/noc/rootfs.squashfs.hi3516ev300 -o up.ev300 --pack -i --cma 'mmz=anonymous,0,0x42000000,96M mmz_allocator=cma'

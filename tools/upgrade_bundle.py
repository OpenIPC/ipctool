#!/usr/bin/env python3

import json
import argparse
import os
import hashlib
import tarfile


template = {
    "kernelMem": "${osmem}",
    "setTotalMem": True,
    "mtdPrefix": "hi_sfc:",
    "osmem": "32M",
    "partitions": [],
}

valid_parts = {'boot': 0x40000, 'kernel': 0x200000, 'rootfs': 0x500000}

# OpenIPC's NOR layouts. With one of these the bundle names no prefix and
# skips nothing: ipctool keeps whatever the camera has in front of its kernel
# (the bootloader and its environment, under this flash's own names), edits
# bootargs and bootcmd instead of replacing them, and carries the settings
# into the new rootfs_data.
layouts = {
    'nor8m': {'kernel': 0x200000, 'rootfs': 0x500000},
    'nor16m': {'kernel': 0x300000, 'rootfs': 0xa00000},
}


def from_firmware(tgz):
    """The kernel and rootfs out of an OpenIPC release archive."""
    found = {}
    with tarfile.open(tgz) as t:
        for m in t.getmembers():
            base = os.path.basename(m.name)
            if base.startswith('uImage.') and not base.endswith('.md5sum'):
                found['kernel'] = t.extractfile(m).read()
            elif base.startswith('rootfs.squashfs.') and \
                    not base.endswith('.md5sum'):
                found['rootfs'] = t.extractfile(m).read()
    if set(found) != {'kernel', 'rootfs'}:
        raise Exception(f'{tgz}: no uImage.* and rootfs.squashfs.* in it')
    return found


def write_layout_bundle(layout, blobs, env, output):
    sizes = layouts[layout]
    bundle = {'partitions': []}
    if env:
        bundle['env'] = env
    data = []
    for name in ('kernel', 'rootfs'):
        content = blobs[name]
        if len(content) > sizes[name]:
            raise Exception(f'{name} is {len(content)} bytes, the {layout} '
                            f'partition {sizes[name]}')
        bundle['partitions'].append({
            'name': name,
            'payloadSize': len(content),
            'partitionSize': sizes[name],
            'sha1': hashlib.sha1(content).hexdigest()[:8],
        })
        data.append(content)
    with open(output, mode='wb') as o:
        o.write(json.dumps(bundle).encode('utf-8'))
        o.write(b'\0')
        for d in data:
            o.write(d)


def add_additional(original, value):
    if original != '':
        original += ' '
    original += value
    return original


def write_bundle(**kwarg):
    cma = kwarg['cma']
    pack = kwarg['pack']
    if not kwarg['boot']:
        template['skip'] = ['boot', 'env']

    additional = ''
    if kwarg['init']:
        additional = add_additional(additional, 'init=/init')
    if cma:
        additional = add_additional(additional, cma)
        template["kernelMem"] = "${totalmem}"
    if additional != '':
        template['additionalCmdline'] = additional

    parts = dict((k, v) for k, v in kwarg.items() if k in valid_parts and v)

    blobs = []
    for name, filename in parts.items():
        size = os.stat(filename).st_size
        with open(filename, mode='rb') as f:
            filecontent = f.read()
            sha1 = hashlib.sha1()
            sha1.update(filecontent)
            digest = sha1.hexdigest()[:8]
            part = {
                'name': name,
                'payloadSize': size,
                'sha1': digest
            }
            if not pack or name == 'boot':
                pSize = valid_parts[name]
                if size > pSize:
                    raise Exception('binary size exceeds partition')
                part['partitionSize'] = pSize
            template['partitions'].append(part)
            blobs.append(filecontent)

    with open(kwarg['output'], mode='wb') as o:
        o.write(json.dumps(template).encode('utf-8'))
        o.write(b'\0')
        for i in blobs:
            o.write(i)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-b", "--boot", help="uboot image file")
    parser.add_argument("-k", "--kernel", help="kernel image file")
    parser.add_argument("-r", "--rootfs", help="rootfs image file")
    parser.add_argument("-o", "--output", help="output filename",
                        required=True)
    parser.add_argument("-c", "--cma", help="cma allocator parameters")
    parser.add_argument("-i", "--init", action="store_true",
                        help="add /init/init")
    parser.add_argument("-p", "--pack", action="store_true",
                        help="pack partitions tightly")
    parser.add_argument("-l", "--layout", choices=sorted(layouts),
                        help="move to this OpenIPC NOR layout, keeping the "
                        "bootloader and the settings")
    parser.add_argument("-f", "--firmware",
                        help="OpenIPC release .tgz to take -k and -r from")
    parser.add_argument("-e", "--env", action="append", default=[],
                        metavar="KEY=VALUE",
                        help="U-Boot variable to set (repeatable)")
    args = parser.parse_args()
    if args.layout:
        blobs = from_firmware(args.firmware) if args.firmware else {}
        for name in ('kernel', 'rootfs'):
            if getattr(args, name):
                with open(getattr(args, name), 'rb') as f:
                    blobs[name] = f.read()
        if set(blobs) != {'kernel', 'rootfs'}:
            parser.error('--layout needs a kernel and a rootfs '
                         '(-f, or -k and -r)')
        if args.boot or args.cma or args.init or args.pack:
            parser.error('--layout keeps the bootloader and its bootargs; '
                         '-b, -c, -i and -p do not apply')
        env = {}
        for kv in args.env:
            key, sep, value = kv.partition('=')
            if not sep or not key:
                parser.error(f'--env {kv}: expected KEY=VALUE')
            if key in ('bootargs', 'bootcmd', 'bootcmdnor', 'mtdparts'):
                parser.error(f'--env {key}: ipctool writes it for the new '
                             'layout itself')
            env[key] = value
        write_layout_bundle(args.layout, blobs, env, args.output)
        return
    if args.firmware or args.env:
        parser.error('-f and -e go with --layout')
    write_bundle(**vars(args))


if __name__ == '__main__':
    main()

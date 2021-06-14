#!/usr/bin/env python3

import json
import argparse
import os
import hashlib


template = {
    "kernelMem": "${totalmem}",
    "setTotalMem": True,
    "mtdPrefix": "hi_sfc:",
    "osmem": "32M",
    "partitions": [],
}

valid_parts = {'boot': 0x50000, 'kernel': 0x200000, 'rootfs': 0x500000}


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
            if not pack:
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
    args = parser.parse_args()
    write_bundle(**vars(args))


if __name__ == '__main__':
    main()

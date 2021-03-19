#!/usr/bin/env python3

import argparse
import yaml
import struct
import hashlib


def cstr_len(data):
    for i in range(len(data)):
        if data[i] == 0:
            return i
    return None


def analyze(filename, extract=False, full=False):
    with open(filename, "rb") as f:
        data = f.read()
        yaml_len = cstr_len(data)
        if yaml_len is None:
            print("Broken dump")
            return

        try:
            descr = yaml.safe_load(data[:yaml_len])
        except yaml.YAMLError as exc:
            print(exc)
            return

        if full:
            ff = open("ff.img", "wb")

        partitions = descr["rom"][0]["partitions"]
        ptr = yaml_len + 1
        for i in range(len(partitions)):
            if ptr >= len(data):
                break

            (next_len, ) = struct.unpack("<I", data[ptr:ptr+4])
            ptr += 4

            partition = partitions[i]
            name = partition["name"]
            psize = partition["size"]
            if psize != next_len:
                print(f"For '{name}' expected size {hex(psize)}, actual"
                      f" {hex(next_len)}, skipping ❌")
                continue

            chunk = data[ptr:ptr+psize]
            sha1 = partition.get("sha1", "")
            if sha1 != "":
                m = hashlib.sha1()
                m.update(chunk)
                short_hash = m.hexdigest()[:8]
                if short_hash != partition["sha1"]:
                    print(f"Checking SHA1 digest failed, "
                          f"expected {short_hash} ❌")
                    continue

            if extract:
                with open(name, "wb") as b:
                    print(f"Writing {name} {psize//1024}Kb...")
                    b.write(chunk)
            if full:
                ff.write(chunk)
            else:
                print(f"✅  {name: <10}{hex(psize): <16}{sha1}")
            ptr += next_len
        if ptr != len(data):
            print("Broken dump")

        if full:
            ff.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("filename", help="binary file with dump")
    parser.add_argument("-e", "--extract", action="store_true",
                        help="Automatically extract binary content")
    parser.add_argument("-f", "--full", action="store_true", help="Write all"
                        "partitions to single file")
    args = parser.parse_args()
    analyze(args.filename, args.extract, args.full)


if __name__ == '__main__':
    main()

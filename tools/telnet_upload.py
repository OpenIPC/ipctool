#!/bin/python3

import argparse
import telnetlib

argparser = argparse.ArgumentParser()
argparser.add_argument('--host', required=True)
argparser.add_argument('--port', type=int, default=23)
argparser.add_argument('--user', type=str, default="root")
argparser.add_argument('--password', type=str, default="12345")
argparser.add_argument('src')
args = argparser.parse_args()

print("Connect to: " + args.host)
t = telnetlib.Telnet(args.host, args.port)

t.read_until(b"login")
t.write(args.user.encode() + b"\n")

t.read_until(b"Password")
t.write(args.password.encode() + b"\n")

payload = open(args.src, 'rb')
payload_lines = payload.readlines()

t.write(b"cd /tmp\n")
t.write(b"rm -f ipctool\n")

for line in payload_lines:
  t.write(b"echo -ne '" + line.strip() + b"' >> ipctool\n")

t.write(b"chmod 755 ipctool\n")
t.mt_interact()

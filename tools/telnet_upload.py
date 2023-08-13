#!/bin/python3

import os, sys, telnetlib, _thread, urllib.request

arm = "https://github.com/openipc/ipctool/releases/download/latest/ipctool"
mips = "https://github.com/openipc/ipctool/releases/download/latest/ipctool-mips32"

port = 23
name = "ipctool"

size = 200
path = "/tmp/ipctool"

def transfer():
    code = "rm -f " + path + "\n"
    t.write(code.encode())
    for index in range(0, len(file), size):
        data = file[index : index + size]
        text = "\\x".join(["{:02x}".format(x) for x in data])
        code = "echo -ne '\\x" + text.strip() + "' >> " + path + "\n"
        t.write(code.encode())
    code = "chmod 755 " + path + "\n"
    t.write(code.encode())
    code = path + "\n"
    t.write(code.encode())

def interact():
    _thread.start_new_thread(t.listener, ())
    while True:
        line = sys.stdin.readline()
        if line.strip() == "transfer":
            transfer()
        else:
            t.write(line.encode())

if len(sys.argv) < 2:
    print("Usage:", sys.argv[0], "[host] [port]")
    exit()

host = sys.argv[1]
if len(sys.argv) > 2:
    port = sys.argv[2]

try:
    urllib.request.urlretrieve(arm, name)
    file = open(name, "rb").read()
except:
    print("Cannot download or access", name)
    exit()

print("Connect to:", host + ":" + str(port))
try:
    t = telnetlib.Telnet(host, port, timeout=5)
except:
    print("Cannot connect to host")
else:
    interact()

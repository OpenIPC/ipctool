#!/bin/python3

import os, sys, _thread, urllib.request
from Exscript.protocols import telnetlib

port = 23
name = "ipctool"
link = "https://github.com/openipc/ipctool/releases/download/latest/ipctool"

size = 200
path = "/tmp/ipctool"

def transfer():
    t.write("rm -f " + path + "\n")
    for index in range(0, len(file), size):
        data = file[index : index + size]
        text = data.hex("-").replace("-", "\\x")
        t.write("echo -ne '\\x" + text.strip() + "' >> " + path + "\n")
    t.write("chmod 755 " + path + "\n")
    t.write(path + "\n")

def interact():
    _thread.start_new_thread(t.listener, ())
    while True:
        line = sys.stdin.readline()
        if line.strip() == "transfer":
            transfer()
        else:
            t.write(line)

if len(sys.argv) < 2:
    print("Usage:", sys.argv[0], "[host] [port]")
    exit()

host = sys.argv[1]
if len(sys.argv) > 2:
    port = sys.argv[2]

try:
    urllib.request.urlretrieve(link, name)
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

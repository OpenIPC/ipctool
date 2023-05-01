#!/bin/python3

import os, sys, _thread
from Exscript.protocols import telnetlib

port = 23

def transfer():
    payload = open(file, 'r')
    t.write("rm -f /tmp/ipctool\n")
    for line in payload.readlines():
        t.write("echo -ne '" + line.strip() + "' >> /tmp/ipctool\n")
    t.write("chmod 755 /tmp/ipctool\n")
    t.write("cd /tmp\n")

def interact():
    _thread.start_new_thread(t.listener, ())
    while True:
        line = sys.stdin.readline()
        if line.strip() == "transfer":
            transfer()
        else:
            t.write(line.encode())

if len(sys.argv) < 3:
    print("Usage:", sys.argv[0], "[file] [host] [port]")
    exit()

file = os.path.join(os.getcwd(), sys.argv[1])
if not os.path.isfile(file):
    print("File not found:", file)
    exit()

host = sys.argv[2]
if len(sys.argv) > 3:
    port = sys.argv[3]

print("Connect to:", host + ":" + str(port))
try:
    t = telnetlib.Telnet(host, port, timeout=5)
except:
    print("Cannot connect to host")
else:
    interact()

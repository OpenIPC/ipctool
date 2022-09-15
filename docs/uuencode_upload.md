# **Uploading files over Telnet + uudecode**

One slow but effective method which can be used if the target device has no way of file upload is to use uuencoded files

Since they only contain printable characters, they can be sent over telnet without issues


***How to check if your target is supported?***

Login over telnet and run "busybox uudecode -h" - if it is installed it will print an error message

## **Enough theory, how do I do it?**

 Install python2, pip2 and tqdm 

     sudo apt install python2 && wget https://bootstrap.pypa.io/pip/2.7/get-pip.py && sudo python2 get-pip.py && sudo pip2 install tqdm

create "sender.py" and insert this code:

    #!/usr/bin/env python
    
    #encode file with uuencode -m ipctool ipctool > ipctoolu
    #run script with  python sender.py --host IP ipctoolu
    
    import argparse
    import telnetlib
    from tqdm import tqdm
    
    
    argparser = argparse.ArgumentParser()
    argparser.add_argument('--host', required=True)
    argparser.add_argument('--port', type=int, default=23)
    argparser.add_argument('--user', type=str, default="root")
    argparser.add_argument('--password', type=str, default="ivideo")
    
    argparser.add_argument('src')
    args = argparser.parse_args()
    
    # Connect to the cam
    t = telnetlib.Telnet(args.host, args.port)
    #t.set_debuglevel(4)
    
    # handle login prompt
    t.read_until(b'(none) login: ', timeout=1)
    t.write(args.user + b'\n')
    
    t.read_until(b'Password: ', timeout=1)
    t.write(args.password + b'\n')
    
    #bad test if we are logged in
    print("If this takes 10+ secs something is wrong...")
    expected_sh = b'~ # '
    t.read_until(expected_sh, timeout=10)
    t.write(b'echo "test" > /tmp/testf\n')
    t.read_until(expected_sh, timeout=10)
    print("Did it? I am too lazy to implement a check xD")
    
    #load file
    payload = open(args.src, 'r')
    Payload_Lines = payload.readlines()
    
    expected_sh_2 = b"/tmp # "
    
    print("If this takes 10 secs something is wrong...")
    t.write(b'cd /tmp;F=payload;true>$F;chmod +x $F\n')
    r = t.read_until(expected_sh_2, timeout=10)
    
    print("Pushing file, go and grab a coffee :)")
    for line in tqdm(Payload_Lines):
        t.write(b'echo "' + line.strip() + '" >> $F' + b'\n')
        r = t.read_until(expected_sh_2, timeout=10)
    
    print("Captain speaking: File arrived at destination, we are now going to convert it back. hehe")
    #decode on target
    t.write(b'busybox uudecode payload\n')
    r = t.read_until(expected_sh_2, timeout=5)
    #make executable on target
    t.write(b'chmod +x ipctool\n')
    r = t.read_until(expected_sh_2, timeout=5)
    
    print("Done :)")

**Note:** if your target uses a different login prompt than "(none) login: " change it in the script!

**uuencode your file you want to send:**

example: `uuencode -m ipctool ipctool > ipctoolu`

**run the script:**

example: `python2.7 sender.py --host 123.456.789.12 --password super_secure_pass_from_passwd_dump ipctoolu`

Login to your target, go to /tmp and et voalià, ipctool is on your camera :)

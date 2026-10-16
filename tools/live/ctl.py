#!/usr/bin/env python3
"""ctl.py k KEYS [HOLD] ... | s NAME | q : sequences of commands to the running game's FIFO.
 e.g.  ctl.py k Escape k E 0.3 s menu"""
import sys,time,os
from PIL import Image
D='/tmp/biohazard-live'
F=D+'/c'
def send(l):
    fd=os.open(F,os.O_WRONLY|os.O_NONBLOCK); os.write(fd,(l+"\n").encode()); os.close(fd)
a=sys.argv[1:]; i=0
while i<len(a):
    if a[i]=='k':
        keys=a[i+1].replace(' ','_'); hold=0.3
        if i+2<len(a) and a[i+2].replace('.','',1).isdigit(): hold=float(a[i+2]); i+=1
        send(f"key {keys} {hold}"); time.sleep(hold+0.35); i+=2
    elif a[i]=='w': time.sleep(float(a[i+1])); i+=2
    elif a[i]=='s':
        p0=f"{D}/shots/{a[i+1]}.png"
        if os.path.exists(p0): os.remove(p0)
        send("snap "+a[i+1])
        for _ in range(100):
            time.sleep(0.2)
            if os.path.exists(p0): time.sleep(0.3); break
        p=f"{D}/shots/{a[i+1]}.png"
        Image.open(p).convert('RGB').resize((640,560)).save(f"{D}/shots/{a[i+1]}_s.png"); print(f"{D}/shots/{a[i+1]}_s.png"); i+=2
    elif a[i]=='q': send("quit"); i+=1

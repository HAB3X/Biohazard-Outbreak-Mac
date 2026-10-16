#!/usr/bin/env python3
"""sweep.py: a lawnmower walk, pressing E at every stop and closing a file screen when one opens."""
import sys,time,os
from PIL import Image
D='/tmp/biohazard-live'
def send(l):
    fd=os.open(D+'/c',os.O_WRONLY|os.O_NONBLOCK); os.write(fd,(l+"\n").encode()); os.close(fd)
def key(k,h=0.3): send(f"key {k} {h}"); time.sleep(h+0.3)
def snap(n):
    p=f"{D}/shots/{n}.png"
    if os.path.exists(p): os.remove(p)
    send("snap "+n)
    for _ in range(100):
        time.sleep(0.2)
        if os.path.exists(p): time.sleep(0.3); break
    return Image.open(p).convert('RGB')
def tabs(im):  # a file/item screen has a bright tab bar at the top
    w,h=im.size; px=im.crop((int(w*0.2),0,int(w*0.8),int(h*0.12))).resize((96,16)).getdata()
    return sum(1 for r,g,b in px if r>200 and g>110 and b<80)
legs=[('A',4),('W',3)]+[x for i in range(5) for x in (('D',1.6),('S' if i%2==0 else 'W',3.5))]
found=0
for i,(d,h) in enumerate(legs):
    key(d,h)
    key('E',0.3); time.sleep(1.2)
    im=snap(f'sw{i}')
    t=tabs(im)
    print(i,d,h,'tabs',t,flush=True)
    if t>0:
        found+=1; im.save(f'{D}/shots/file_{i}.png')
    key('Left_Shift'); key('Left_Shift')
print('files',found)

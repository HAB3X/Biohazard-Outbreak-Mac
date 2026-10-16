#!/usr/bin/env python3
"""bot.py STEPS [SEED]: wanders, and presses E (then Left Shift) when a red CHECK! prompt shows."""
import sys,time,os,random
from PIL import Image
D='/tmp/biohazard-live'
def send(l):
    fd=os.open(D+'/c',os.O_WRONLY|os.O_NONBLOCK); os.write(fd,(l+"\n").encode()); os.close(fd)
def key(k,h=0.3): send(f"key {k} {h}"); time.sleep(h+0.3)
n=0
def snap(name):
    send("snap "+name); time.sleep(1.3); return Image.open(f"{D}/shots/{name}.png").convert('RGB')
def check(im):
    w,h=im.size; px=im.crop((0,int(h*0.7),w,h)).resize((192,64)).getdata()
    return sum(1 for r,g,b in px if r>110 and g<55 and b<55)
random.seed(int(sys.argv[2]) if len(sys.argv)>2 else 1)
dirs=['W','W','A','D','S','W+D','W+A']
for i in range(int(sys.argv[1])):
    key('E',0.2)
    d=random.choice(dirs); h=random.choice([0.8,1.2,1.8])
    key(d,h)
    if i%3: continue
    im=snap('bot')
    c=check(im)
    if c>3:
        print(i,'CHECK',c,flush=True)
        im.save(f'{D}/shots/check_{i}.png')
        key('E'); time.sleep(1); key('E'); time.sleep(0.5); key('Left_Shift'); time.sleep(0.5)
print('done')

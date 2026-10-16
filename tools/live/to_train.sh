#!/bin/sh
cd /tmp/biohazard-live
./start.sh ${1:-file2}; sleep 32
python3 ctl.py k Escape 0.5 w 6 k Escape 0.5 w 6 k E w 4 k E w 6 k E w 4 k E w 4 k E w 4 w 40 s train

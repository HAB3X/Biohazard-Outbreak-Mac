#!/bin/sh
# to_wild.sh [GAME]: a fresh game, into the second scenario with the first character, easy
cd /tmp/biohazard-live
./start.sh ${1:-file2}; sleep 32
python3 ctl.py k Escape 0.5 w 6 k Escape 0.5 w 6 k E w 4 k E w 6 k Right w 3 k E w 4 k E w 4 k E w 4 k E w 3 k Up w 1 k E w 4 w 100 s wild

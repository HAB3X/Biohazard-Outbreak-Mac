#!/bin/sh
# start.sh GAME : a fresh game with the control FIFO
pgrep -f "build/biohazard-file" | xargs kill -9 2>/dev/null; sleep 1
cd /tmp/biohazard-live; rm -rf shots card c; mkdir -p shots card
cd ~/Documents/biohazard-outbreak-mac
nohup ./run.sh $1 --headless --run-seconds 6000 --fps --control /tmp/biohazard-live/c --snapshots /tmp/biohazard-live/shots --memcards /tmp/biohazard-live/card --settings /tmp/biohazard-live/card > /tmp/biohazard-live/game.log 2>&1 &

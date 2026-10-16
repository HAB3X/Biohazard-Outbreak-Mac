# Playing with a look at the picture

`start.sh GAME` runs a game headless with the runtime's `--control` FIFO (commands: `key NAMES HOLD`, `snap NAME`, `quit`) and a
snapshot folder, all under /tmp/biohazard-live (edit D in the scripts to move it). `ctl.py k Escape 0.5 w 6 s name`
sends keys, waits, and takes a picture (`shots/name_s.png`, scaled). `to_train.sh` and `to_wild.sh` go through the menus to
File 2's first and second scenarios. `bot.py` wanders and presses E at a red CHECK! prompt; `sweep.py` walks a lawnmower
pattern pressing E and closing file screens. Snapshots slow the game (they read the picture back): take frame rates from the
`--fps` lines when nothing is taking pictures.

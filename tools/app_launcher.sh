#!/bin/sh
# The release app's executable (tools/make_app.py --release puts it in Contents/MacOS with @GAME@, @EXE@ and @NAME@ filled in):
# the game finds the player's disc image (asking for it the first time) and checks it's this game's; the English patch is built
# from that disc into the settings folder when there's none, the disc or the app changed, or bindings.txt is newer than it (the
# patch's key caps show the keys bound); then the game starts in English. Options given to the app (open -a APP --args ...) are
# passed on to the game. Nothing outside the app is used but the system's tools, the disc and the settings folder.
CONTENTS="$(cd "$(dirname "$0")/.." && pwd)"
GAME="$CONTENTS/MacOS/@EXE@"
BUILDER="$CONTENTS/Resources/english-patch"
PYTHON="$CONTENTS/Resources/python"

alert() { # title, message
    /usr/bin/osascript - "$1" "$2" >/dev/null 2>&1 <<'EOF'
on run argv
    display alert (item 1 of argv) message (item 2 of argv) as critical
end run
EOF
}

# macOS's old -psn_ argument isn't an option of the game's. --prepare (the launcher's): only the English patch made if it's
# due, with no dialog, its progress in $BH_PROGRESS's file (tools/bh_build_patch.py); the game isn't started
PREPARE=0
for a in "$@"; do
    shift
    case "$a" in -psn_*) ;; --prepare) PREPARE=1 ;; *) set -- "$@" "$a" ;; esac
done

# The disc and the settings folder, a line each; a wrong disc or none chosen is said by the game itself
FOUND="$("$GAME" --disc-check "$@")" || exit 1
DISC="$(printf '%s\n' "$FOUND" | sed -n 1p)"
SETTINGS="$(printf '%s\n' "$FOUND" | sed -n 2p)"
PATCH="$SETTINGS/english.patch"
MADE_FROM="$SETTINGS/english.patch.made-from"
LOG="$SETTINGS/english-patch.log"
WANTED="$(cat "$BUILDER/VERSION")
$DISC"

if [ ! -f "$PATCH" ] || [ "$(cat "$MADE_FROM" 2>/dev/null)" != "$WANTED" ] || [ "$SETTINGS/bindings.txt" -nt "$PATCH" ]; then
    if [ "$PREPARE" = 1 ]; then
        env -i PYTHONHOME="$PYTHON" PYTHONNOUSERSITE=1 PYTHONDONTWRITEBYTECODE=1 PATH=/usr/bin:/bin LANG=en_US.UTF-8 \
            PS2_RUNTIME_DIR="$BUILDER/ps2-native-runtime" BH_SETTINGS="$SETTINGS" @GAME_UPPER@_DISC="$DISC" \
            BH_PROGRESS="$BH_PROGRESS" "$PYTHON/bin/python3" "$BUILDER/tools/bh_build_patch.py" @GAME@ "$PATCH.new" >"$LOG" 2>&1 || {
            rm -f "$PATCH.new"
            tail -n 3 "$LOG" >&2
            exit 1
        }
        mv -f "$PATCH.new" "$PATCH"
        printf '%s\n' "$WANTED" >"$MADE_FROM"
        exit 0
    fi
    env -i PYTHONHOME="$PYTHON" PYTHONNOUSERSITE=1 PYTHONDONTWRITEBYTECODE=1 PATH=/usr/bin:/bin LANG=en_US.UTF-8 \
        PS2_RUNTIME_DIR="$BUILDER/ps2-native-runtime" BH_SETTINGS="$SETTINGS" @GAME_UPPER@_DISC="$DISC" \
        "$PYTHON/bin/python3" "$BUILDER/tools/bh_build_patch.py" @GAME@ "$PATCH.new" >"$LOG" 2>&1 &
    BUILD=$!
    # The wait, said in a dialog whose Quit stops it; the dialog is closed when the patch is made
    # (what it answers is kept: Quit pressed says so; a dialog that couldn't be shown, headless or refused, stops nothing)
    ANSWER="$SETTINGS/english-patch.answer"
    rm -f "$ANSWER"
    /usr/bin/osascript - "$CONTENTS/Resources/AppIcon.icns" >"$ANSWER" 2>/dev/null <<'EOF' &
on run argv
    display dialog "Making the English patch from your disc image. This takes less than a minute, the first time and after your keys change." buttons {"Quit"} default button "Quit" with title "@NAME@" with icon (POSIX file (item 1 of argv))
end run
EOF
    WAIT=$!
    while kill -0 "$BUILD" 2>/dev/null; do
        if ! kill -0 "$WAIT" 2>/dev/null && grep -q "Quit" "$ANSWER" 2>/dev/null; then
            { kill "$BUILD"; wait "$BUILD"; } 2>/dev/null # (the shell's "Terminated" notice kept out of the log)
            rm -f "$PATCH.new"
            exit 1
        fi
        sleep 0.2
    done
    wait "$BUILD"
    STATUS=$?
    { kill "$WAIT"; wait "$WAIT"; } 2>/dev/null
    rm -f "$ANSWER"
    if [ "$STATUS" -ne 0 ]; then
        rm -f "$PATCH.new"
        alert "The English patch couldn't be made" "$(tail -n 3 "$LOG")

If macOS asked whether the app may access the folder your disc image is in and you chose Don't Allow, allow it in System Settings > Privacy & Security > Files & Folders, then open the app again.

The whole report is in $LOG: please include it with a bug report."
        exit 1
    fi
    mv -f "$PATCH.new" "$PATCH"
    printf '%s\n' "$WANTED" >"$MADE_FROM"
fi

[ "$PREPARE" = 1 ] && exit 0
exec "$GAME" --disc "$DISC" --patch "$PATCH" "$@"

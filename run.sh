#!/bin/sh
# Runs a game from the build with your disc image: ./run.sh file1|file2 [options]. The disc is FILE1_DISC / FILE2_DISC, or
# local.json's "file1" / "file2" path.
GAME="$1"; shift
case "$GAME" in file1|file2) ;; *) echo "usage: $0 file1|file2 [options]" >&2; exit 2 ;; esac
VAR=$(echo "$GAME" | tr a-z A-Z)_DISC
DISC="$(eval echo \"\$$VAR\")"
[ -n "$DISC" ] || DISC="$(python3 -c "import json;print(json.load(open('$(dirname "$0")/local.json'))['$GAME'])" 2>/dev/null)"
APP="$(dirname "$0")/build/biohazard-$GAME"
exec "$APP" --disc "$DISC" "$@"

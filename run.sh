#!/bin/sh
# Runs a game from the build with your disc image: ./run.sh file1|file2 [options]. The disc is FILE1_DISC / FILE2_DISC, or
# local.json's "file1" / "file2" path.
GAME="$1"; shift
case "$GAME" in file1|file2) ;; *) echo "usage: $0 file1|file2 [options]" >&2; exit 2 ;; esac
VAR=$(echo "$GAME" | tr a-z A-Z)_DISC
DISC="$(eval echo \"\$$VAR\")"
[ -n "$DISC" ] || DISC="$(python3 -c "import json;print(json.load(open('$(dirname "$0")/local.json'))['$GAME'])" 2>/dev/null)"
APP="$(dirname "$0")/build/biohazard-$GAME"
# Rooms edited with tools/bh_rdt_edit.py (work/rooms-GAME.patch) are laid over the disc when the file exists
ROOMS="$(dirname "$0")/work/rooms-$GAME.patch"
[ -f "$ROOMS" ] && set -- --patch "$ROOMS" "$@"
# English: ./run.sh file2 --english builds build/english-file2.patch from your disc and the translations, then plays with it
case " $* " in *" --english "*)
  set -- $(echo "$@" | sed 's/--english//')
  python3 "$(dirname "$0")/tools/bh_build_patch.py" "$GAME" "$(dirname "$0")/build/english-$GAME.patch" || exit 1
  exec "$APP" --disc "$DISC" --patch "$(dirname "$0")/build/english-$GAME.patch" "$@" ;;
esac
exec "$APP" --disc "$DISC" "$@"

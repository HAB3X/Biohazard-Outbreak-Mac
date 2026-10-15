#!/usr/bin/env python3
"""The functions in an address range of an executable, each with the functions calling it (JAL) from outside the range:
a library's entry points are the ones called from outside it.

    callers.py CONFIG ELF START END
"""
import bisect
import os
import sys

RUNTIME = os.environ.get("PS2_RUNTIME_DIR", os.path.expanduser("~/Documents/woc-agents/biohazard/ps2-native-runtime"))
sys.path.insert(0, os.path.join(RUNTIME, "recomp"))

import recomp  # noqa: E402
from analysis import Program  # noqa: E402
from elf import Elf  # noqa: E402
from image import CodeImage  # noqa: E402


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    config = recomp.load_config(sys.argv[1])
    p = Program(CodeImage.from_elf(Elf(sys.argv[2]), config["text"], config["vutext"]), config)
    functions = p.analyse()
    lo, hi = int(sys.argv[3], 16), int(sys.argv[4], 16)
    starts = [s for s, _ in functions]
    callers = {}
    for i in p.insns:
        if i.kind == "call" and lo <= i.target < hi:
            f = starts[bisect.bisect_right(starts, i.addr) - 1]
            callers.setdefault(i.target, set()).add(f)
    for s, e in functions:
        if lo <= s < hi:
            outside = sorted(c for c in callers.get(s, ()) if not lo <= c < hi)
            inside = sorted(c for c in callers.get(s, ()) if lo <= c < hi)
            print(f"{s:08x}-{e:08x} outside: {' '.join(f'{c:08x}' for c in outside) or '-'}"
                  f"   inside: {len(inside)}")


if __name__ == "__main__":
    main()

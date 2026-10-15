#!/usr/bin/env python3
"""Where an executable's code builds an address (lui + addiu/ori, the usual way code reaches a string or a table), and the
function each such place is in, by the recompiler's own analysis.

    xref.py CONFIG ELF ADDRESS...      (addresses in hex; a string's file offset is not its address)
"""
import os
import sys

RUNTIME = os.environ.get("PS2_RUNTIME_DIR", os.path.expanduser("~/Documents/woc-agents/biohazard/ps2-native-runtime"))
sys.path.insert(0, os.path.join(RUNTIME, "recomp"))

import recomp  # noqa: E402
from analysis import Program, writes_gpr  # noqa: E402
from elf import Elf  # noqa: E402
from image import CodeImage  # noqa: E402


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    config = recomp.load_config(sys.argv[1])
    p = Program(CodeImage.from_elf(Elf(sys.argv[2]), config["text"], config["vutext"]), config)
    functions = p.analyse()
    wanted = {int(a, 16) for a in sys.argv[3:]}
    starts = [s for s, _ in functions]
    import bisect
    for n, i in enumerate(p.insns):
        if i.name != "lui":
            continue
        for j in p.insns[n + 1:n + 12]:
            if j.name in ("addiu", "daddiu", "ori") and j.rs == i.rt:
                low = j.uimm if j.name == "ori" else j.imm
                v = ((i.uimm << 16) + low) & 0xFFFFFFFF
                if v in wanted:
                    f = starts[bisect.bisect_right(starts, j.addr) - 1]
                    print(f"{v:08x} built at {j.addr:08x} in the function at {f:08x}")
                break
            if writes_gpr(j) == i.rt:
                break


if __name__ == "__main__":
    main()

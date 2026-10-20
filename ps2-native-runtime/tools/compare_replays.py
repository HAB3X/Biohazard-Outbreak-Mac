#!/usr/bin/env python3
"""Compares two replays' pictures (--gs-replay with --snapshots): the vblanks' hashes in replay.txt, and the PNGs both folders
have, pixel by pixel.

    compare_replays.py A B [--sample-a N] [--sample-b N] [--diff DIR] [--top K]

A picture drawn at a scale is compared at the PS2's pixels with --sample-a / --sample-b N: each N x N block's first pixel (the
GS's sample point, as the renderers read targets back). --diff writes each differing pair's difference (amplified) as PNG.
Needs Pillow and NumPy.
"""

import argparse
import os
import sys

import numpy as np
from PIL import Image


def read_hashes(folder):
    hashes = {}
    path = os.path.join(folder, "replay.txt")
    if not os.path.exists(path):
        return hashes
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 3:
                hashes[int(parts[0])] = (parts[1], parts[2])
    return hashes


def load(path, sample):
    picture = np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)
    if sample > 1:
        picture = picture[::sample, ::sample]
    return picture


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--sample-a", type=int, default=1)
    parser.add_argument("--sample-b", type=int, default=1)
    parser.add_argument("--diff")
    parser.add_argument("--top", type=int, default=5, help="the pictures that differ most, listed")
    args = parser.parse_args()

    ha = read_hashes(args.a)
    hb = read_hashes(args.b)
    both = sorted(set(ha) & set(hb))
    same = sum(1 for v in both if ha[v] == hb[v])
    print(f"hashes: {len(both)} vblanks in both, {same} the same")

    names = sorted(n for n in os.listdir(args.a) if n.endswith(".png") and os.path.exists(os.path.join(args.b, n)))
    if not names:
        print("pictures: none in both")
        return 0
    if args.diff:
        os.makedirs(args.diff, exist_ok=True)
    identical = 0
    rows = []
    total_pixels = 0
    total_differing = 0
    worst = 0
    for name in names:
        a = load(os.path.join(args.a, name), args.sample_a)
        b = load(os.path.join(args.b, name), args.sample_b)
        if a.shape != b.shape:
            rows.append((1.0, name, f"sizes differ: {a.shape[1]}x{a.shape[0]} and {b.shape[1]}x{b.shape[0]}"))
            continue
        difference = np.abs(a - b)
        per_pixel = difference.max(axis=2)
        differing = int(np.count_nonzero(per_pixel))
        total_pixels += per_pixel.size
        total_differing += differing
        if differing == 0:
            identical += 1
            continue
        largest = int(per_pixel.max())
        worst = max(worst, largest)
        share = differing / per_pixel.size
        over8 = int(np.count_nonzero(per_pixel > 8))
        rows.append((share, name, f"{differing} pixels ({100 * share:.2f}%) differ, {over8} by more than 8, at most {largest}, "
                                  f"mean {float(per_pixel[per_pixel > 0].mean()):.1f} where they do"))
        if args.diff:
            Image.fromarray(np.clip(difference * 8, 0, 255).astype(np.uint8)).save(os.path.join(args.diff, name))
    print(f"pictures: {len(names)} in both, {identical} identical; {total_differing} of {total_pixels} pixels differ "
          f"({100 * total_differing / max(total_pixels, 1):.3f}%), at most by {worst}")
    for share, name, text in sorted(rows, reverse=True)[: args.top]:
        print(f"  {name}: {text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/bin/sh
# Outbreak Studio: ./studio.sh [file1|file2 [SCENARIO]] [--port N] [--no-open]
cd "$(dirname "$0")" && exec python3 tools/bh_studio.py "$@"

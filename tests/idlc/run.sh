#!/usr/bin/env bash
# 字节级对照：冻结的手写实现（tests/idlc/legacy）vs idlc 生成的编解码。
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/.gen"
rm -rf "$OUT"
python3 "$REPO/tools/idlc.py" "$REPO/contracts/vehicle_status.yaml" "$OUT"
cd "$HERE"
g++ -std=c++17 -O0 -Dvehicle_status=vs_a -I"$HERE/legacy" -c legacy.cpp -o "$OUT/legacy.o"
g++ -std=c++17 -O0 -Dvehicle_status=vs_b -I"$OUT" -c gen.cpp -o "$OUT/gen.o"
g++ -std=c++17 -O0 -I"$HERE" -c main.cpp -o "$OUT/main.o"
g++ "$OUT/main.o" "$OUT/legacy.o" "$OUT/gen.o" -o "$OUT/btest"
"$OUT/btest"

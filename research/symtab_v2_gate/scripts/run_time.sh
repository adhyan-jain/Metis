#!/usr/bin/env bash
# Timed runs, pinned to P-core 4 (4.9 GHz max), governor=powersave (cannot change without root).
cd "$(dirname "$0")/.."
for spec in "lua 15" "cjson 15" "protobufc 15" "freertos 15" "redisdeps 15" "redis 9"; do
  set -- $spec
  taskset -c 4 ./harness time $1 $2 8 > raw/time_$1.csv 2> raw/time_$1.err
done
echo done > raw/time_done.flag

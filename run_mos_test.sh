#!/usr/bin/env bash
set -e
gcc -o test_mos test_mos.c -lm
./test_mos
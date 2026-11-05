#!/bin/sh
# Reproduce: single-core rows pinned to one core, then the 4-thread DRAM rows.
set -e
gcc -O3 -ffast-math -fopenmp -march=native warden_bench.c -o warden_bench
# optional 2nd arg: all|mem|cache|fma|render (parts append to the csv); BENCH_MIB sets the DRAM array size (default 512)
OMP_NUM_THREADS=1 taskset -c 2 ./warden_bench single_O3.csv
OMP_NUM_THREADS=$(nproc) OMP_PROC_BIND=true ./warden_bench quad_O3.csv
# -O2 build: the lambert row is only valid here (at -O3 -ffast-math the compiler hoists the constant inputs).
gcc -O2 -fopenmp -march=native warden_bench.c -o warden_bench_O2
OMP_NUM_THREADS=1 taskset -c 2 ./warden_bench_O2 single_O2.csv
# Encode cost: see encode.txt for the ffmpeg command lines.

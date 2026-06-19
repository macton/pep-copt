#!/bin/sh
# measure-speedup.sh — the single measurement protocol for the PEP harness.
#
#   measure-speedup.sh <harness> <qbin-list> <out-dir>
#
# Runs <harness> on the given input 5 times back-to-back, parses the printed
# "total_compress_seconds <value>" each time, and reports the 5 samples and the
# MEDIAN. WSL2 is noisy, so a single run is not evidence — every speedup number
# in this project comes from the median of 5.
#
# Speedup is computed by the caller as ref-median / opt-median.
set -e

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <harness> <qbin-list> <out-dir>" >&2
    exit 2
fi
HARNESS="$1"
LIST="$2"
OUTDIR="$3"

# Per-run timeout: generous (a normal run is a few seconds; default 600s is ~150x
# headroom) but bounds a hung/broken harness so measurement fails fast instead of
# spinning forever. Override via PER_RUN_TIMEOUT.
PER_RUN_TIMEOUT="${PER_RUN_TIMEOUT:-600}"

i=1
samples=""
while [ "$i" -le 5 ]; do
    s=$(timeout "$PER_RUN_TIMEOUT" "$HARNESS" "$LIST" "$OUTDIR" "$OUTDIR/_measure_results.txt" \
        | sed -n 's/^total_compress_seconds \([0-9.][0-9.]*\)/\1/p')
    if [ -z "$s" ]; then
        echo "measure-speedup: harness produced no timing on run $i (possible timeout after ${PER_RUN_TIMEOUT}s or crash)" >&2
        exit 1
    fi
    echo "  sample $i: $s s"
    samples="$samples $s"
    i=$((i+1))
done

median=$(printf '%s\n' $samples | sort -n | sed -n '3p')
echo "median:  $median s"

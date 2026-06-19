#!/bin/sh
# prove-optimized-harness.sh — end-to-end proof of the optimized PEP library vs
# the reference, on the committed images:
#
#   images/*  --build_input(quantize)-->  build/q/*.qbin     (excluded from timing)
#   reference harness  <- build/q/*.qbin  ->  build/out/*.pep      + results.txt
#   optimized harness  <- build/q/*.qbin  ->  build/out-opt/*.pep  + results.txt
#   compare_results: the optimized output, ONCE DECOMPRESSED, must be
#                    PIXEL-IDENTICAL to the reference's decompressed output (=
#                    the quantized input). The .pep file format/bytes MAY differ
#                    between the two builds, but the optimized .pep must NOT be
#                    LARGER than the reference .pep (per image) — speed may not be
#                    bought with a bigger file. Both sides must round-trip
#                    losslessly.
#
# This is the identity-baseline proof (src-optimized is a verbatim copy of src):
# expect pixel-identical decompressed output (here also byte-identical, being a
# copy), 0 round-trip failures, and speedup ~1.0x (identical code cannot be
# faster than itself; any deviation is WSL2 noise). The locked strict
# size-correct baseline is ~2x (never larger than reference); see
# src-optimized/OPTIMIZATION-LOG.md for the measured speed/size frontier.
#
# Output: by default only the FINAL SUMMARY + verdict reach stdout (every step
# still goes to the log); pass --verbose (-v) to stream every step.
set -e
cd "$(dirname "$0")"
B=build
PTO=performance-test-optimized
LOG="$PTO/proof-run.log"
MEASURE="$PTO/measure-speedup.sh"
SEEDS="0xA5A5A5A5DEADBEEF 0x0123456789ABCDEF"

# Per-step / per-measure timeouts. These are deliberately generous so a slow but
# CORRECT step is never killed: a single harness run on the committed images is
# ~4 s and quantization is ~8 s, so STEP_TIMEOUT (20 min) is ~150x headroom, and
# even a 50x-slower-than-reference optimization finishes far inside it. The point
# is only to bound a BROKEN binary that hangs forever (infinite loop), turning a
# deadlock into a fast, visible failure. Override via env if a host needs more.
STEP_TIMEOUT="${STEP_TIMEOUT:-1200}"      # any single build/run/test step (s)
MEASURE_TIMEOUT="${MEASURE_TIMEOUT:-1800}" # a whole median-of-5 measure call (s)
export PER_RUN_TIMEOUT="${PER_RUN_TIMEOUT:-600}"  # one harness run inside measure (s)
DECOMP_RUNS="${DECOMP_RUNS:-5}"           # median-of-N for the decompression timing
# Net-neutral band: the optimized decode may be at most this fraction slower than
# the reference decode and still count as "net-neutral" (absorbs measurement
# noise). The intent is parity-or-better; a real regression must FAIL.
DECOMP_TOL="${DECOMP_TOL:-0.03}"

VERBOSE=0
for arg in "$@"; do
    case "$arg" in -v|--verbose) VERBOSE=1 ;; esac
done

: > "$LOG"
say()    { echo "$@" >> "$LOG"; [ "$VERBOSE" = 1 ] && echo "$@"; return 0; }
report() { echo "$@" | tee -a "$LOG"; }
run() {
    echo "\$ timeout ${STEP_TIMEOUT}s $*" >> "$LOG"
    # NOTE: a failing step must NOT abort the script — gate steps (compare_results,
    # test_runner_opt, ...) are *expected* to be able to fail, and the FINAL
    # SUMMARY/ENFORCING GATE below collects those verdicts. The timeout command is
    # guarded by `if ... then ... else` (set -e exempt) so a non-zero exit is
    # recorded, not fatal. (A bare `timeout ...` here would trip `set -e`.)
    if [ "$VERBOSE" = 1 ]; then
        timeout "$STEP_TIMEOUT" "$@" 2>&1 | tee -a "$LOG"
    elif timeout "$STEP_TIMEOUT" "$@" >> "$LOG" 2>&1; then
        :
    else
        rc=$?
        [ "$rc" = 124 ] && echo "[TIMEOUT after ${STEP_TIMEOUT}s — step killed]" >> "$LOG"
    fi
    return 0
}
median_of() { timeout "$MEASURE_TIMEOUT" "$MEASURE" "$1" "$2" "$3" | sed -n 's/^median:  \([0-9.]*\) s/\1/p'; }
ratio() { awk -v r="$1" -v o="$2" 'BEGIN{ if (o>0) printf "%.2f", r/o; else print "n/a" }'; }
manifest_sum() { find images -type f | sort | xargs sha256sum | sha256sum | cut -d' ' -f1; }

say "================================================================"
say " PROVE OPTIMIZED HARNESS — PEP optimized library vs reference"
say " date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
say "================================================================"

say ""
say "### source under test: src-optimized/"
if diff -q src/pep.h src-optimized/pep.h >/dev/null 2>&1 \
   && diff -q src/pep.c src-optimized/pep.c >/dev/null 2>&1; then
    say "  src-optimized is a verbatim copy of src — IDENTITY baseline (expect pixel-identical, ~1.0x)."
else
    say "  src-optimized differs from the reference — a real optimization is under test."
fi

CK_BEFORE=$(manifest_sum)
say "committed images sha256 (before): $CK_BEFORE"

# --- STEP 1: clean build -----------------------------------------------
say ""
say "### STEP 1: clean build of reference + optimized (-Wall -Wextra -Werror)"
run make clean
run make all
run make -f Makefile.optimized optimized

# --- STEP 2: build-stage quantization (excluded from timing) -----------
say ""
say "### STEP 2: build-stage quantization (images/ -> build/q/*.qbin)"
PRECOMP_LIMIT=3600   # 60 minutes
P0=$(date +%s.%N)
run make input
P1=$(date +%s.%N)
PRECOMP=$(awk -v a="$P0" -v b="$P1" 'BEGIN{printf "%.3f", b-a}')
say "  total quantization time: ${PRECOMP} s (limit ${PRECOMP_LIMIT} s; excluded from compression time)"
if awk -v p="$PRECOMP" -v l="$PRECOMP_LIMIT" 'BEGIN{exit !(p>l)}'; then
    say "ERROR: quantization time ${PRECOMP}s exceeds the ${PRECOMP_LIMIT}s limit"
    exit 1
fi

# --- STEP 3: reference run ---------------------------------------------
say ""
say "### STEP 3: reference harness -> build/out/*.pep"
run ./build/perf_harness build/images.list build/out performance-test/results.txt

# --- STEP 4: optimized run ---------------------------------------------
say ""
say "### STEP 4: optimized harness -> build/out-opt/*.pep"
run ./build/perf_harness_opt build/images.list build/out-opt "$PTO/results.txt"

# --- STEP 5: decompressed-pixel comparator -----------------------------
say ""
say "### STEP 5: compare_results — decompressed pixels identical + lossless round-trip + opt .pep not larger than ref"
run ./build/compare_results performance-test/results.txt "$PTO/results.txt"

# --- STEP 6: full reference suite vs optimized library -----------------
say ""
say "### STEP 6: full correctness suite against the optimized library"
run ./build/test_runner_opt

# --- STEP 7: median-of-5 speedup on the committed images ---------------
say ""
say "### STEP 7: median-of-5 compression speedup on the committed images"
REF_MED=$(median_of ./build/perf_harness     build/images.list build/out)
OPT_MED=$(median_of ./build/perf_harness_opt build/images.list build/out-opt)
SPEEDUP=$(ratio "$REF_MED" "$OPT_MED")
say "  ref median ${REF_MED} s | opt median ${OPT_MED} s | speedup ${SPEEDUP}x"

# --- STEP 7b: decompression-time gate (opt decode must be net-neutral or better)
say ""
DECOMP_TOL_PCT=$(awk -v t="$DECOMP_TOL" 'BEGIN{printf "%.0f", t*100}')
say "### STEP 7b: decompression-time gate — optimized decode must be <= reference (within ${DECOMP_TOL_PCT}% noise band)"
REF_DEC=$(timeout "$STEP_TIMEOUT" ./build/decomp_time     "$DECOMP_RUNS" build/out/*.pep     2>>"$LOG" | sed -n 's/^decompress_median \([0-9.][0-9.]*\).*/\1/p')
OPT_DEC=$(timeout "$STEP_TIMEOUT" ./build/decomp_time_opt "$DECOMP_RUNS" build/out-opt/*.pep 2>>"$LOG" | sed -n 's/^decompress_median \([0-9.][0-9.]*\).*/\1/p')
DECOMP_RATIO=$(ratio "$OPT_DEC" "$REF_DEC")   # opt/ref ; <=1.0 means net-neutral-or-better
DECOMP_OK=$(awk -v o="$OPT_DEC" -v r="$REF_DEC" -v t="$DECOMP_TOL" 'BEGIN{ if (r=="" || o=="") {print 0; exit} print (o <= r*(1+t)) ? 1 : 0 }')
say "  ref decode ${REF_DEC:-?} s | opt decode ${OPT_DEC:-?} s | opt/ref ${DECOMP_RATIO}x | gate $( [ "$DECOMP_OK" = 1 ] && echo PASS || echo FAIL )"

# --- STEP 8: generalization on synthetic alternate inputs --------------
say ""
say "### STEP 8: generalization on synthetic alternate inputs (>=2 seeds)"
n=0
GEN_OK=1
for seed in $SEEDS; do
    n=$((n+1))
    gdir="$B/gen_$n"; qdir="$B/genq_$n"; list="$B/gen_$n.list"
    say ""
    say "-- alternate set $n (seed $seed) --"
    run ./build/gen_images_seed "$gdir" "$seed" 4 256
    run make IMAGES_DIR="$gdir" QDIR="$qdir" LIST="$list" input
    run ./build/perf_harness     "$list" "$B/genref_$n" "$B/genref_$n.res"
    run ./build/perf_harness_opt "$list" "$B/genopt_$n" "$B/genopt_$n.res"
    if ./build/compare_results "$B/genref_$n.res" "$B/genopt_$n.res" >> "$LOG" 2>&1; then
        say "  set $n: PASS (pixel-identical)"
    else
        say "  set $n: FAIL"
        GEN_OK=0
    fi
    rmed=$(median_of ./build/perf_harness     "$list" "$B/genref_$n")
    omed=$(median_of ./build/perf_harness_opt "$list" "$B/genopt_$n")
    say "  set $n: ref median ${rmed} s | opt median ${omed} s | speedup $(ratio "$rmed" "$omed")x"
done

# --- STEP 9: determinism -----------------------------------------------
say ""
say "### STEP 9: determinism — two optimized runs produce byte-identical .pep + manifest"
# Both runs target the SAME output dir so the manifest's pep_path column matches;
# determinism then means identical manifests AND identical .pep bytes (overwritten).
run ./build/perf_harness_opt build/images.list "$B/det" "$B/det_a.res"
run ./build/perf_harness_opt build/images.list "$B/det" "$B/det_b.res"
if diff -q "$B/det_a.res" "$B/det_b.res" >/dev/null 2>&1; then DET="byte-identical"; else DET="DIFFER (NON-DETERMINISTIC)"; fi
say "  determinism: $DET"

# --- checksum after ----------------------------------------------------
CK_AFTER=$(manifest_sum)
say ""
say "committed images sha256 (after):  $CK_AFTER"
if [ "$CK_BEFORE" != "$CK_AFTER" ]; then
    say "ERROR: committed images changed during the proof run"
    exit 1
fi

# --- FINAL SUMMARY (all values measured) -------------------------------
set +e
CMP=$(./build/compare_results performance-test/results.txt "$PTO/results.txt" 2>&1)
IMAGES=$(printf '%s\n' "$CMP"   | sed -n 's/^images \([0-9][0-9]*\).*/\1/p')
RT_FAIL=$(printf '%s\n' "$CMP"  | sed -n 's/^round_trip_failures \([0-9][0-9]*\).*/\1/p')
PIXEL_MM=$(printf '%s\n' "$CMP" | sed -n 's/^pixel_mismatches \([0-9][0-9]*\).*/\1/p')
SIZE_REG=$(printf '%s\n' "$CMP" | sed -n 's/^size_regressions \([0-9][0-9]*\).*/\1/p')
SIZE_RATIO=$(printf '%s\n' "$CMP" | sed -n 's/^opt_size_vs_ref \([0-9.][0-9.]*\).*/\1/p')
CMP_VERDICT=$(printf '%s\n' "$CMP" | grep -E '^(PASS|FAIL)$' | tail -1)
SUITE=$(grep -E '[0-9]+ passed, [0-9]+ failed' "$LOG" | tail -1)

report ""
report "================================================================"
report " FINAL SUMMARY — measured (committed images)"
report "================================================================"
report "  comparator:            ${CMP_VERDICT:-UNKNOWN} (images ${IMAGES:-?}, pixel-mismatches ${PIXEL_MM:-?}, round-trip failures ${RT_FAIL:-?}, size-regressions ${SIZE_REG:-?}, opt .pep size vs ref ${SIZE_RATIO:-?}x)"
report "  match criterion:       decompressed pixels identical + lossless round-trip + opt .pep not larger than ref (per image)"
report "  full test suite:       ${SUITE:-UNKNOWN}"
report "  determinism:           ${DET}"
report "  generalization:        $( [ "$GEN_OK" = 1 ] && echo 'PASS (all alternate sets pixel-identical)' || echo 'FAIL' )"
report "  quantization time:     ${PRECOMP:-?} s (build stage; excluded from compression time)"
report "  reference median:      ${REF_MED:-?} s  (compression only)"
report "  optimized median:      ${OPT_MED:-?} s  (compression only)"
report "  speedup (committed):   ${SPEEDUP}x   (strict size-correct baseline ~2x; higher only with a size tolerance — see OPTIMIZATION-LOG.md frontier)"
report "  decompression:         ref ${REF_DEC:-?} s | opt ${OPT_DEC:-?} s | opt/ref ${DECOMP_RATIO:-?}x  ($( [ "$DECOMP_OK" = 1 ] && echo 'net-neutral-or-better' || echo 'REGRESSION' ); must be <= ref +${DECOMP_TOL_PCT}%)"
report "  committed images sha256: $CK_AFTER (unchanged)"
report "================================================================"

# --- ENFORCING GATE ----------------------------------------------------
GATE_OK=1
[ "$CMP_VERDICT" = "PASS" ]   || { report "GATE FAIL: comparator (byte-identity / round-trip) did not pass"; GATE_OK=0; }
[ "$DET" = "byte-identical" ] || { report "GATE FAIL: optimized output is non-deterministic"; GATE_OK=0; }
[ "$GEN_OK" = 1 ]             || { report "GATE FAIL: generalization sets diverged"; GATE_OK=0; }
echo "$SUITE" | grep -q '0 failed' || { report "GATE FAIL: correctness suite had failures"; GATE_OK=0; }
[ "$DECOMP_OK" = 1 ]          || { report "GATE FAIL: optimized decompression slower than reference (must be net-neutral or better)"; GATE_OK=0; }
if [ "$GATE_OK" -ne 1 ]; then
    report "PROOF FAILED"
    exit 1
fi
report "PROOF COMPLETE"

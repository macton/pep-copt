# PEP Image Recompression — Optimized Test Harness (Scaffold)

Instructions for an LLM agent. Build the **test, comparison, and measurement
scaffold** that `create-optimized.md`'s optimization loop will use to prove an
optimized PEP compressor is correct and fast. In this task the optimized library
is a **verbatim copy of the reference** `src/`, so the scaffold must demonstrate
the trivial identity baseline with real command output: **no measurable speedup
(~1.0x)**, **every produced `.pep` decompressing to pixels identical to the
reference's decompressed pixels** (for the identity copy the `.pep` bytes also
happen to match, but that is not what is checked), **0 round-trip failures
(lossless)**, and the full reference test suite passing against the copy. Never
report a step as complete without running it; never fabricate results or
measurements.

If the scaffold reports anything other than this identity baseline when both
libraries are byte-identical copies, the scaffold is wrong — fix it before
finishing.

## Why this exists

`create-optimized.md` requires, on every iteration, that you compare an
optimized compressor against the reference, measure a median-of-5 speedup, run
the full reference test suite against the optimized library, and run a
generalization check on alternate inputs. That machinery must exist and be
proven correct **before** any real optimization is attempted — otherwise a
passing report proves nothing. This task builds it and proves it against the one
input for which the answer is known exactly: an identical copy.

The data-oriented point: the core of this scaffold is a **results comparator** —
a pure data transform. Input: two results manifests in the fixed reference
format. Output: a match verdict. The match criterion is **pixel-identity after
decompression**, not byte-identity of the compressed files and not a tolerance:
PEP must be **lossless** (decompression reproduces the quantized input exactly),
so a correct optimization — even one that changes the file format or entropy
coder — must, once decompressed, yield the **same pixels** as the reference's
decompressed output. The harness decompresses each `.pep` (the reference build
decompresses reference `.pep`, the optimized build decompresses optimized `.pep`)
and the comparator compares the **decompressed pixels** (via the `decoded_fnv`
digest the harness records), never the compressed bytes. When the two producing
libraries are identical, that transform must yield PASS with zero pixel
mismatches. Build and verify that transform first; everything else is plumbing.

## Read first (do not modify any of these)

- `create-reference.md` and `create-optimized.md` (same directory) — the
  contracts this scaffold serves.
- `../src/pep.h`, `../src/pep.c` — the reference compressor (the copy's starting
  point); `../src/pep_codec.{h,c}` — the shared pre-processing library.
- `../test/test_main.c` — the correctness suite.
- `../performance-test/build_input.c`, `../performance-test/perf_main.c` — the
  build-stage quantizer and the timing harness.
- `../images/` — the committed input images (read-only baseline).
- `../Makefile`, `../run-performance-test`.

The committed `images/`, the reference `src/`, `test/`, and the reference
harness are the comparison baseline. **Never modify them.** Do not write into
`images/`. Do not alter existing reference build targets.

## Fixed facts to reproduce (from create-optimized.md — do not relax)

- **Manifest format** (one line per image, already produced by the reference
  harness): `idx w h colors model pep_bytes pep_fnv decoded_fnv round_trip_ok
  pep_path`.
- **Match criterion** (all must hold): for every image, (1) the optimized
  output's **decompressed pixels are identical to the reference's decompressed
  pixels** — compared via the `decoded_fnv` digest of the decompressed RGBA, not
  the compressed bytes; (2) **dimensions match**; (3) `round_trip_ok == 1` on
  both sides (each build's decompressor losslessly reproduces the quantized
  input); and (4) the optimized `.pep` is **not larger** than the reference
  `.pep` (`opt.pep_bytes <= ref.pep_bytes`, **per image**). This is
  **pixel-identity after decompression** — **never** a tolerance, and **never** a
  comparison of the `.pep` bytes. The compressed file format/`model`/`colors`
  **MAY differ** between the two builds (the optimizer is allowed to change the
  entropy coder / format), and the size **may differ** — but only **downward or
  equal**, never larger (otherwise "faster" could just be a cheaper-but-bigger
  encoding, defeating the point of a compressor). The comparator matches rows by
  index + basename and compares `decoded_fnv` + `round_trip_ok` + `pep_bytes`,
  never a bare `diff` of the manifests (the `pep_path` column legitimately
  differs).
- **Measurement protocol**: run a harness 5 times back-to-back, take the
  **median** of `total_compress_seconds`. WSL2 is noisy; single runs are not
  evidence. Speedup = reference median ÷ optimized median.
- **Pre-processing (quantization) is excluded from the timed compression** and
  must be a general transform over any image directory.
- **No overfitting**: nothing keyed to the committed images.

## Deliverables (create exactly these; start from copies)

    src-optimized/                 verbatim copy of src/pep.{c,h}; the compressor
                                   create-optimized.md will edit. (pep_codec is
                                   shared from src/ — quantization is not under
                                   optimization.)
    performance-test-optimized/    the comparator, alternate-input generator,
                                   measurement script, and the optimized results
    run-performance-test-optimized project-root script, same contract as
                                   run-performance-test, running the optimized
                                   harness and writing
                                   performance-test-optimized/results.txt

Build wiring: add optimized targets in a separate `Makefile.optimized` that
compile `src-optimized/pep.c` and the optimized harness into `build/` under
**distinct** names (`pep_opt.o`, `perf_harness_opt`, `test_runner_opt`,
`compare_results`, `gen_images_seed`, `decomp_time`, `decomp_time_opt`),
**without changing any reference target**.
Same flags as the reference (so the speedup ratio is honest). The optimized
harness is the **same** `performance-test/perf_main.c`, compiled with
`-Isrc-optimized -Isrc -I.` so `#include "pep.h"` resolves to the optimized copy
— the include path is the only difference.

### The comparator (build this first, it is the core)

`performance-test-optimized/compare_results.c` → `build/compare_results`.

- Usage: `compare_results <reference-results> <optimized-results>`.
- Reads both manifests in the fixed format.
- **Out-of-range / malformed behavior is explicit and loud**: differing image
  counts, a malformed line, or a row that does not line up by index + basename →
  print a specific error and exit non-zero. Never silently skip.
- For each image: require dimensions equal, require `round_trip_ok == 1` on both
  sides, **compare the decompressed-pixel digest `decoded_fnv` (reference vs
  optimized)**, and require **`opt.pep_bytes <= ref.pep_bytes`** (the optimized
  file is not larger). Do **not** compare `pep_fnv` or the `.pep` bytes, and do
  **not** require `model`/`colors` to match — those may legitimately differ once
  the format can change.
- Prints, in a fixed parseable form: `images`, `round_trip_failures`,
  `dim_mismatches`, `pixel_mismatches`, `size_regressions`,
  `max_size_regression_bytes`, `ref_total_pep_bytes`, `opt_total_pep_bytes`,
  `opt_size_vs_ref` (informational), and `PASS`/`FAIL`.
- Exit 0 only when `round_trip_failures`, `dim_mismatches`, `pixel_mismatches`,
  and `size_regressions` are all zero. Exit non-zero otherwise.

This is the comparator `create-optimized.md` reuses; treat it as the contract
boundary.

### Optimized harness and tests

- Compile `performance-test/perf_main.c` with the optimized include path into
  `build/perf_harness_opt` (links `pep_opt.o` + shared `pepc.o`). Keep the
  harness logic identical — do not weaken the timing, round-trip, or determinism
  behavior.
- Compile `test/test_main.c` against the optimized library into
  `build/test_runner_opt` — the **full reference correctness suite** run against
  the optimized compressor. Do not copy-and-weaken the tests.

### Alternate-input generation (for the generalization check)

`performance-test-optimized/gen_images_seed.c` → `build/gen_images_seed`: a
deterministic synthetic **low-color pixel-art** image generator (PEP's intended
domain), seeded from the command line, writing PNGs the reference `build_input`
then quantizes like any other input. Same seed → byte-identical images. It
shares no code with the compressor.

### Measurement script

`performance-test-optimized/measure-speedup.sh`: runs a given harness on a given
`.qbin` list 5 times, parses `total_compress_seconds`, prints the 5 samples and
the **median**. This is the single measurement protocol used everywhere.

### Decompression-time tool (the decode gate)

`performance-test-optimized/decomp_time.c`, compiled **twice** (`build/decomp_time`
with `-Isrc`, `build/decomp_time_opt` with `-Isrc-optimized -Isrc`), so each
build decompresses `.pep` files made by its own compressor/format. Usage:
`decomp_time <runs> <file.pep> ...` — it `pep_load`s every file up front (file
I/O excluded), then times `pep_decompress` over the whole set `<runs>` times and
prints `decompress_median <s>`. The proof uses it to enforce the **decode gate**:
the optimized compressor optimizes *compression*, but it must not regress
*decompression* — `opt_decode <= ref_decode` within a small noise band
(`DECOMP_TOL`, default 3%). Faster decode is welcome; a real decode regression is
a FAIL even if compression hit the target.

### Top-level proof driver

`prove-optimized-harness.sh` at project root: runs the whole scaffold end to end
on the identity copy and prints a FINAL SUMMARY. Steps, each with real output:

1. Clean build of reference and optimized targets (zero warnings).
2. Build-stage quantization (`make input`), timed and capped (excluded from the
   compression time used for speedup).
3. `./build/perf_harness …` → `performance-test/results.txt` + `build/out/*.pep`.
4. `./build/perf_harness_opt …` → `performance-test-optimized/results.txt` +
   `build/out-opt/*.pep`.
5. `compare_results` → expect decompressed pixels identical (0 pixel mismatches),
   0 round-trip failures, PASS. (For the identity copy the `.pep` bytes also match,
   but the comparator only checks the decompressed pixels.)
6. `build/test_runner_opt` → full suite passes against the copy.
7. Median-of-5 for both harnesses → report both medians and the speedup. Expect
   **≈1.0x (no measurable gain)**; state explicitly that any deviation from 1.0
   is pure WSL2 measurement noise (identical code cannot be faster than itself).
7b. **Decode gate**: median-of-`DECOMP_RUNS` of `decomp_time` (reference `.pep`)
    vs `decomp_time_opt` (optimized `.pep`) → expect `opt_decode <= ref_decode`
    within `DECOMP_TOL`. For the identity copy this is ≈1.0x; a real decode
    regression FAILS the proof.
8. Generalization: generate **≥2** synthetic alternate sets, run both harnesses,
   `compare_results` → expect PASS (pixel-identical) on every set.
9. Determinism: two optimized runs to the **same** output dir produce
   byte-identical manifests and `.pep` files (determinism is still byte-level:
   the *same* code on the *same* input must emit the *same* bytes every run —
   this is unchanged by the relaxed match criterion).
10. Final summary block: pixel-identical PASS, 0 round-trip failures, 0 size
    regressions, **decode net-neutral-or-better**, suite pass, generalization
    pass, determinism confirmed, committed images checksum unchanged, speedup
    ≈1.0x with the target (10x) noted.

Save the summary to `performance-test-optimized/HARNESS-BASELINE.md` (the
identity baseline, with actual commands and output as evidence).

## Done means (identity baseline, with evidence)

- Builds clean; libc/libm + vendored single-headers only; single-threaded.
- `compare_results` on the committed images: decompressed pixels identical (0
  pixel mismatches), 0 round-trip failures, 0 size regressions (no optimized
  `.pep` larger than the reference's), PASS.
- `test_runner_opt`: full suite passes against the copy.
- Speedup ≈1.0x reported as **no measurable gain**, samples and medians shown;
  noise labeled as noise.
- Decode gate: `decomp_time_opt` median <= `decomp_time` median within
  `DECOMP_TOL` (≈1.0x for the identity copy).
- Generalization: ≥2 synthetic sets, each pixel-identical PASS.
- Determinism: two optimized runs byte-identical (same code, same bytes).
- Committed `images/` checksum unchanged; reference `src/`, `test/`, and the
  reference harness untouched.

## Hand-off to create-optimized.md

State explicitly in `HARNESS-BASELINE.md` that the optimization loop reuses,
unchanged: `build/compare_results` as the decompressed-pixel comparator,
`measure-speedup.sh` as the median-of-5 protocol, `build/test_runner_opt` as the
correctness gate, `build/gen_images_seed` for the generalization check,
`build/decomp_time` / `build/decomp_time_opt` as the decode gate, and
`run-performance-test-optimized` as the optimized run contract. The optimization
work then edits only `src-optimized/` (compressor **and** its decompressor — the
decode gate ties them together), with this identity baseline as iteration-0
evidence that the measurement pipeline is sound.

## Acceptance checklist

- [ ] `src-optimized/pep.{c,h}` is a verbatim copy of `src/` at start; reference
      `src/` untouched; `pep_codec` shared from `src/`.
- [ ] Optimized build targets are separate from reference targets; clean build.
- [ ] `build/compare_results` implements the decompressed-pixel + round-trip +
      size-not-larger criterion (compares `decoded_fnv`, NOT the `.pep` bytes;
      `.pep` format may differ and size may shrink but never grow per image), with
      loud failure on malformed/mismatched inputs; exit code reflects PASS/FAIL.
- [ ] `build/test_runner_opt` runs the full reference suite against the optimized
      library and passes.
- [ ] `build/gen_images_seed` generates deterministic low-color alternate inputs.
- [ ] `build/decomp_time` / `build/decomp_time_opt` implement the decode gate
      (median-of-N `pep_decompress`, file load excluded); the proof FAILS on a
      decode regression beyond `DECOMP_TOL`.
- [ ] `measure-speedup.sh` implements the median-of-5 protocol.
- [ ] `prove-optimized-harness.sh` reports the identity baseline: ≈1.0x,
      pixel-identical PASS, 0 round-trip failures, 0 size regressions, decode
      net-neutral (≈1.0x), suite pass, generalization pass, determinism, checksum
      unchanged.
- [ ] `HARNESS-BASELINE.md` records the proof with real command output and the
      hand-off note to `create-optimized.md`.
- [ ] No unmeasured performance claim anywhere; noise labeled as noise.

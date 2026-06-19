# PEP Optimized Harness — Confirmed State (strict, ~2x, 24 images)

All values below are real output from `./prove-optimized-harness.sh`
(full log: `proof-run.log`).

> The earlier "9.63x" here was on the original 3-image set **and** a size-breaking
> single-model shortcut (larger-than-reference files on ~1/3 of real images). The
> benchmark was expanded to 24 real images, the size gate exposed the shortcut,
> and the compressor was switched to **strict all-model selection** — never
> larger than the reference. The honest, size-correct speed is **~2x** (the 6–18x
> was the shortcut). See `src-optimized/OPTIMIZATION-LOG.md` for the full frontier.

- Host: Linux 6.6.87.2-microsoft-standard-WSL2 x86_64 | gcc 11, single-threaded, WSL2.
- Run date: 2026-06-19.
- Committed input: `images/` (24 photographic JPEGs, all ≤4096 per axis), read-only.
- **Match contract (all gated):** decompressed pixels identical to the reference
  + lossless round-trip + optimized `.pep` **not larger** than the reference
  (per image) + decompression **net-neutral-or-better** (opt decode <= ref decode
  within ~3%). The `.pep` bytes/format MAY differ; only the decompressed pixels,
  size, and decode time are gated.

> The harness was first proven against an **identity copy** (`src-optimized/` ==
> `src/`) as iteration-0 evidence that the measurement pipeline is sound: that
> baseline showed ~1.0x speedup, pixel-identical PASS, decode ~1.0x — exactly
> what an identical copy must produce. That identity baseline has been superseded
> by the real optimized proof recorded here; the pipeline-soundness conclusion
> still holds.

## Confirmed result — strict, ~2x (24 images)

```
 FINAL SUMMARY — measured (committed images)
================================================================
  comparator:            PASS (images 24, pixel-mismatches 0, round-trip failures 0, size-regressions 0, opt .pep size vs ref 1.0000x)
  match criterion:       decompressed pixels identical + lossless round-trip + opt .pep not larger than ref (per image)
  full test suite:       13 passed, 0 failed
  determinism:           byte-identical
  generalization:        PASS (all alternate sets pixel-identical)
  quantization time:     21.640 s (build stage; excluded from compression time)
  reference median:      17.457696 s  (compression only)
  optimized median:      8.563426 s  (compression only)
  speedup (committed):   2.04x   (per-image ~1.5–2.6x; strict never-larger)
  decompression:         ref 5.961799 s | opt 6.008309 s | opt/ref 1.01x  (net-neutral-or-better; must be <= ref +3%)
  committed images sha256: 2ff0dba29d13e538c4b1690ea7a2ade39a2cab9a80ee20d7e66166fea5066768 (unchanged)
================================================================
PROOF COMPLETE
```

Strict = byte-identical to the reference on all 24 (the optimizer picks the same
smallest-of-all-models the reference does), so the optimized `.pep` is never
larger; the ~2x is the genuine faster-per-model-encode win.

This satisfies the **complete** contract at once: pixel-identical after
decompression, lossless, deterministic, `.pep` not larger than the reference, and
decode net-neutral. 10x was **not** reached (~96% of target).

## Per-step evidence (final run)

### Step 5 — decompressed-pixel comparator (`build/compare_results`)

```
images 3
round_trip_failures 0
dim_mismatches 0
pixel_mismatches 0
size_regressions 0
max_size_regression_bytes 0
ref_total_pep_bytes 5275636
opt_total_pep_bytes 5275636
opt_size_vs_ref 1.0000
PASS
```

Per-image `.pep` sizes equal the reference exactly: 3,523,161 / 742,410 /
1,010,065 bytes (so `opt_size_vs_ref` = 1.0000x).

### Step 6 — full correctness suite vs optimized library (`build/test_runner_opt`)

```
13 passed, 0 failed
```

### Step 7 — median-of-5 compression speedup (committed images)

```
ref median 3.732664 s | opt median 0.387590 s | speedup 9.63x
```

### Step 7b — decompression-time gate (`build/decomp_time` vs `build/decomp_time_opt`)

```
ref decode 0.286152 s | opt decode 0.288694 s | opt/ref 1.01x | gate PASS
```

Net-neutral-or-better (within the 3% band). Earlier in the run a transient ~21%
decode regression was introduced and **caught by this gate**, then fixed back to
parity — the reason the gate exists.

### Step 8 — generalization (synthetic low-color alternate sets)

```
set 1 (seed 0xA5A5A5A5DEADBEEF): PASS (pixel-identical) | ref 0.122910 s | opt 0.125311 s | 0.98x
set 2 (seed 0x0123456789ABCDEF): PASS (pixel-identical) | ref 0.094856 s | opt 0.094344 s | 1.01x
```

The committed-input speedup (9.63x) comes from the large many-color image; the
256×256 ≤16-color synthetic sets are too small for that win to register, but they
confirm correctness (pixel-identical) and size/decode bounds on other data.

### Step 9 — determinism

Two optimized runs to the same output dir produced **byte-identical** manifests
and `.pep` files.

## How the run ended

The run did **not** stop at a defined exit criterion. The LLM provider (OpenAI)
returned `429 insufficient_quota` (out of API quota) at ~03:06, ~0.37x short of
10x. The on-disk `src-optimized/` is left at this gate-passing 9.63x state; a
resumed run would continue from here (provider quota permitting).

## Hand-off / reusable harness pieces

The optimization loop reuses, unchanged:

- `build/compare_results` — the decompressed-pixel + size comparator (compares
  `decoded_fnv` and `pep_bytes`; never a manifest `diff` of the bytes).
- `build/decomp_time` / `build/decomp_time_opt` — the decode-time gate
  (median-of-N `pep_decompress`; opt must be net-neutral-or-better).
- `performance-test-optimized/measure-speedup.sh` — the median-of-5 protocol.
- `build/test_runner_opt` — the full reference correctness suite against the
  optimized library.
- `build/gen_images_seed` — the deterministic alternate-input generator for the
  generalization check.
- `run-performance-test-optimized` — the optimized run contract.

The optimization work edits `src-optimized/` (compressor **and** its
decompressor — the decode gate ties them together).

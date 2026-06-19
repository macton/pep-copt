# PEP optimized compressor — current size-gated state

## Goal and constraints

The target remains **10x median compression speedup** over the reference compressor. The current valid proof is **8.76x**, so the target has **not** been reached.

Required behavior:

- Lossless round-trip: decompressed pixels are identical to reference output.
- Optimized `.pep` size is **no larger than reference per image**.
- Serialized optimized output is deterministic.
- Runtime compression is single-threaded.
- No precomputed streams.

## Data and output

Input is already-quantized `.qbin` data (`PQB1`) with at most 256 colors assumed. Quantization is outside the timed compression path.

The compressor builds a deterministic first-occurrence palette and palette-index stream, then writes a self-contained `.pep` readable by the optimized decompressor. The current valid path preserves reference-sized arithmetic PPM output for the proof set, so the size gate passes at exactly parity.

## Current kept optimizations

- Large-image single-model heuristic:
  - very large high-palette images start with NW,
  - medium high-palette images start with W.
- Direct W/NW no-padding encoder matching padded context semantics.
- Split/peeled boundary loops.
- Local direct arithmetic coder state.
- Palette hash.
- Block-prefix sums.
- Count-only loser evaluation for multi-model small cases.

Small images still use multi-model evaluation so the size gate generalizes; earlier single-model use on all images passed the committed set but failed small synthetic cases.

## Current proof

Latest valid proof after repairing split W/NW and local direct arithmetic state:

```text
comparator:              PASS
full test suite:         13 passed, 0 failed
determinism:             byte-identical
generalization:          PASS
opt .pep size vs ref:    1.0000x
reference median:        3.757814 s
optimized median:        0.429166 s
speedup:                 8.76x
```

Best observed valid proof during this span was about **8.81x** (`ref 3.756186 s`, `opt 0.426490 s`), but the current proof number is **8.76x**.

## Recent rejected or neutral experiments

- Malformed macro issue was fixed.
- Loop-state W/NW carry regressed.
- Direct `void` arithmetic helpers regressed.
- Fused prefix-low was neutral/slower and reverted.
- Split palette-after-256 was neutral/slower and reverted.
- Skipping direct realloc regressed and reverted.

## Remaining work

The compressor is valid under the size gate, deterministic, and lossless, but still below the 10x target. Further work should be measured against the per-image size gate, determinism, full tests, and generalization before being kept.

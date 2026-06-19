# PEP Image Recompression — Reference Implementation

Instructions for an LLM agent. You must **implement, build, test, run, and
verify** this project end to end. "Done" means every item in the Acceptance
Checklist passes with real command output as evidence. Never report a step as
complete without having run it; never fabricate results.

## Goal

Create a reference solution in C that takes ordinary image files (jpg, png, tga,
bmp, gif), recompresses them with **PEP** (Prediction-Encoded Pixels —
`https://github.com/ENDESGA/PEP`, vendored as `src/pep.h`), and writes `.pep`
files. This reference is deliberately a **simple, direct interface** — it is the
baseline that the optimized version (`prompts/create-optimized.md`) and the
proof harness (`prompts/create-optimized-test-harness.md`) are measured against.

PEP is a **palette compressor**: `pep_compress` keeps only the first **≤256
distinct colors** and collapses everything else to palette index 0. Photographic
input therefore must be reduced to ≤256 colors **before** compression, or the
output is visually broken. The reference owns that pre-processing step
(quantization) as well as the compression call.

## Fixed requirements (do not relax any of these)

- Language: C (C11). Build with gcc. Dependencies: **libc + libm only**, plus
  the vendored single-header libraries `pep.h`, `stb_image.h`,
  `stb_image_write.h`. No other third-party code.
- Build flags: `-std=c11 -Wall -Wextra -Werror -O3 -march=native -DNDEBUG`. The
  vendored headers may be compiled with `-Wno-unknown-pragmas` added (and only
  that — `pep.h` uses cosmetic `#pragma region`s gcc does not recognize); all of
  **your** code compiles clean under the full flag set.
- Single-threaded. No threads.
- Layout: `src/` for the library, `test/` for tests, `performance-test/` for the
  timing harness, `run-performance-test` script at the project root, a
  `Makefile`.
- Determinism: same input bytes → same output bytes, every run. Both the
  quantizer and `pep_compress` must be deterministic.
- The committed input images live in `images/` and are **read-only**: the
  harness must never modify them (verify with a checksum before/after).

## The reference interface — replicate exactly this

The reference pipeline, in order, is:

1. **Load** an image as RGBA8 with `stb_image` (`stbi_load(path, …, 4)`).
2. **Quantize** to ≤ `max_colors` (default 256) with **deterministic
   median-cut**: split the box with the most pixels along its widest channel at
   the median; map every pixel to its box's average color. The sort comparator
   must break ties on the full color then pixel index so the result is a pure
   function of the input. This is **pre-processing** — it is excluded from the
   timed compression measurement.
3. **Compress** the quantized RGBA with `pep_compress(pixels, w, h, pep_rgba,
   pep_8bit)`. **This single call is the timed operation.**
4. **Save** with `pep_save(&p, "<name>.pep")`.
5. **Verify** the round-trip: `pep_decompress(&p, pep_rgba, 0, 0)` must return
   pixels **byte-identical** to the quantized input (PEP is lossless on
   palettized data). Free with `pep_free`.

Put this in `src/` as a small library plus the vendored compressor:

- `src/pep.h` — the vendored PEP single-header (the compressor; the optimized
  version will be a copy of this that gets edited).
- `src/pep.c` — `#define PEP_IMPLEMENTATION` then `#include "pep.h"` (the one TU
  that instantiates the compressor).
- `src/pep_codec.h` / `src/pep_codec.c` — the pre-processing library: `pepc_load`,
  `pepc_quantize`, `pepc_free`, a fixed-format quantized-image container
  (`.qbin`: magic `PQB1`, u32 width/height/colors, then RGBA bytes) via
  `pepc_write_qbin` / `pepc_read_qbin`, and FNV-1a digests (`pepc_fnv1a`,
  `pepc_fnv1a_file`) used by the harness to record byte-identity. This TU owns
  the stb implementations and depends on nothing in `pep.h`, so one object is
  shared by the reference and optimized harnesses.

`src/README.md` must briefly state: the data layouts (`pepc_image`, the `.qbin`
format), the pipeline above, why quantization is required and excluded from
timing, and any `ASSUMPTION: <fact> — affects <decision>` you could not confirm.

## Phase 2 — Implement the harness

- `performance-test/build_input.c` → `build/build_input`: the **build-stage
  transform**. Enumerates an images directory (sorted for determinism), loads
  and quantizes each image, writes `build/q/<name>.qbin`, and emits a list file
  of `.qbin` paths. A **general transform of any image directory** — not keyed
  to the committed images. Excluded from timing.
- `performance-test/perf_main.c` → `build/perf_harness`: reads the `.qbin` list,
  and for each image **times only `pep_compress`**, accumulating a total; writes
  `<out-dir>/<name>.pep`; decompresses and verifies the round-trip is lossless;
  records a manifest line per image (`idx w h colors model pep_bytes pep_fnv
  decoded_fnv round_trip_ok pep_path`); and prints
  `total_compress_seconds <value>` — the single number the measurement protocol
  parses. The harness reads inputs only; it never writes into `images/`.
- `test/test_main.c` → `build/test_runner`: the correctness suite (below).
- A `Makefile` building all of the above with the fixed flags, and a
  `make input` target that runs `build_input` on `images/`.
- `run-performance-test` (project root, executable): `make all`, `make input`,
  checksum `images/` before/after, run `perf_harness`, write
  `performance-test/results.txt`, fail if the committed images changed.

## Phase 3 — Test (required coverage)

`test/test_main.c` must cover, each checked with real assertions:

- **Lossless round-trip**: `decompress(compress(x)) == x` for quantized pixels.
- **Determinism**: two compressions of the same pixels produce byte-identical
  serialized output (`pep_serialize`); quantization of the same image is
  byte-identical.
- **Quantization bound**: ≤ requested colors are produced.
- **Save/load**: `pep_save` → `pep_load` → `pep_decompress` matches the input.
- **Degenerate sizes** (1×1).

## Phase 4 — Verify (mandatory, with evidence)

Run and capture output for: clean build (`make clean && make`, zero warnings);
full test suite passing; `run-performance-test` twice with byte-identical
`results.txt` and the committed images' checksum unchanged; and the printed
total compression time. Report what was verified with the actual commands and
output, and state anything not verified and why.

## Acceptance checklist

- [ ] `src/README.md` documents the data layouts, the pipeline, and labeled
      ASSUMPTIONs.
- [ ] Builds clean with `-Wall -Wextra -Werror` (vendored headers only relax
      `-Wno-unknown-pragmas`); libc/libm + vendored single-headers only.
- [ ] The reference pipeline is exactly: load → deterministic median-cut
      quantize (≤256) → `pep_compress` (timed) → `pep_save` → lossless
      round-trip check.
- [ ] All tests pass (round-trip, determinism, quantize bound, save/load, 1×1).
- [ ] `build_input` is a general transform of any image directory; quantization
      is excluded from the timed compression.
- [ ] `perf_harness` prints `total_compress_seconds` and writes a per-image
      manifest; results are deterministic across runs.
- [ ] Committed `images/` never modified (checksum evidence).
- [ ] Verification evidence (commands + output) included in the final report.

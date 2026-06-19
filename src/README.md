# Reference PEP image-recompression library

This is the reference (baseline) implementation: a simple, direct interface that
loads an image, reduces it to a PEP-compatible palette, compresses it with PEP,
and saves a `.pep` file. It is the comparison baseline for the optimized
compressor (`../src-optimized/`) and the proof harness
(`../prove-optimized-harness.sh`).

## Why quantization is required

PEP (`pep.h`, ENDESGA/PEP) is a **palette compressor**: `pep_compress` keeps
only the first **≤256 distinct colors** and collapses everything else to palette
index 0. Photographs have hundreds of thousands of colors, so they must be
reduced to ≤256 colors **before** compression or the output is visually broken.
That reduction is **pre-processing** and is excluded from the timed compression.

## Data layouts

- `pepc_image` — `{ int width; int height; uint8_t *rgba; int colors; }`. RGBA8,
  row-major, `colors` = distinct palette entries after quantization.
- `.qbin` (build artifact, the quantized image the harness compresses):
  `"PQB1"` magic, then little-endian `u32 width`, `u32 height`, `u32 colors`,
  then `width*height*4` RGBA bytes. Written/read by `pepc_write_qbin` /
  `pepc_read_qbin`.
- Results manifest (one line per image, written by the harness):
  `idx w h colors model pep_bytes pep_fnv decoded_fnv round_trip_ok pep_path`.

## The pipeline (the reference interface)

1. **Load** — `pepc_load` → `stbi_load(path, …, 4)` (jpg/png/tga/bmp/gif).
2. **Quantize** — `pepc_quantize(img, max_colors)`: deterministic median-cut.
   Split the box with the most pixels along its widest channel at the median;
   map each pixel to its box's average color. The sort comparator breaks ties on
   the full color then pixel index, so the output is a pure function of the
   input. (Pre-processing; not timed.)
3. **Compress** — `pep_compress(rgba, w, h, pep_rgba, pep_8bit)`. **This single
   call is the timed operation.** PEP tries several prediction models and keeps
   the smallest; the chosen model is recorded.
4. **Save** — `pep_save(&p, "<name>.pep")`.
5. **Round-trip check** — `pep_decompress(&p, pep_rgba, 0, 0)` must equal the
   quantized input **byte-for-byte** (PEP is lossless on palettized pixels).

## Files

- `pep.h` — vendored PEP single-header (the compressor under optimization).
- `pep.c` — the one TU that instantiates it (`PEP_IMPLEMENTATION`).
- `pep_codec.h` / `pep_codec.c` — load, quantize, `.qbin` I/O, FNV-1a digests.
  Owns the stb implementations; depends on nothing in `pep.h`, so one object is
  shared by the reference and optimized harnesses.

## Determinism

Same input bytes → same output bytes, every run: the quantizer is a pure
function of the pixels, and `pep_compress` is deterministic. The test suite and
the proof harness both verify this.

## ASSUMPTIONs

- `ASSUMPTION: median-cut to 256 colors is an acceptable default quality/▒size
  trade-off for the committed photographic images — affects` the quantizer
  default; callers can pass a smaller `max_colors` (PEP is strongest at ≤16
  colors, where it can beat the source JPEG; at 256 on photos it does not).
- `ASSUMPTION: pep_rgba/pep_8bit is the right format/bit-depth for stb's RGBA8
  output — affects` the compress call; verified by the lossless round-trip test.

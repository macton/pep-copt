# PEP Image Recompression — Quality Visualizer

Instructions for an LLM agent. Build a small tool that renders, **one image at a
time**, a side-by-side comparison so a human can confirm by eye what PEP
recompression did: the **original** source image, the **quantized** version
(≤256 colors, the input PEP actually compresses), and the **PEP round-trip**
(decompressed `.pep`). You must **build, run, capture output, and inspect it**
until the comparisons are demonstrably correct. "Done" is the acceptance
checklist, met **with real images and command output as evidence**. Never report
a step complete without running it; never fabricate output.

## What this is for

PEP is lossless on palettized pixels, so the round-trip must equal the quantized
input **exactly** — the visualizer makes that visible (round-trip and quantized
panels are pixel-identical) and, more usefully, lets a human judge the
**quantization quality**: how much was lost going from the original photo to
≤256 colors, which is where all the visible loss happens. It is a **verification
aid**, not codec code — it shares nothing with `src-optimized/` and is never on a
timed path.

## The data is the thing — start here

Read the actual artifacts before designing anything:

- `images/<name>` — the committed source images (read-only).
- `build/q/<name>.qbin` — the quantized RGBA (fixed format: magic `PQB1`, u32
  width/height/colors, then `width*height*4` RGBA bytes), produced by
  `make input`.
- `build/out/<name>.pep` — the reference `.pep`, produced by the harness.
- `performance-test/results.txt` — the manifest: per image, `idx w h colors
  model pep_bytes pep_fnv decoded_fnv round_trip_ok pep_path`.

Decode a `.pep` back to RGBA with `pep_decompress` (from `src/pep.h`) for the
round-trip panel; or read it straight from the harness, which already verified
the round-trip equals the `.qbin`.

### Data facts that constrain the design (verify them yourself)

- The committed images are **large** (e.g. 3561×4728). Downscale for display;
  keep the comparison panels the same scale as each other so differences are
  honest. Do not present resampling artifacts as quantization loss.
- The round-trip panel must be **identical** to the quantized panel (PEP is
  lossless). If it is not, the codec or the tool is wrong — fix the tool's
  decode path or report a real codec failure; do not hide a mismatch.
- All visible loss is from quantization, not from PEP. Label the panels so a
  reader is not misled into thinking PEP is lossy.

## The transform (the whole machine)

```
for each image listed in results.txt:
  load original (stb_image)           -> panel A "original (N distinct colors)"
  read build/q/<name>.qbin            -> panel B "quantized (<=256 colors)"
  pep_load build/out/<name>.pep,
    pep_decompress                    -> panel C "PEP round-trip"
  assert panel C == panel B (bytewise); annotate if it ever differs
  compose A|B|C side-by-side, downscaled, with a caption strip:
    name, WxH, original colors, palette size, source bytes, .pep bytes,
    ratio vs raw RGBA, bpp
  write viz/shots/<name>.png
```

## Technology and self-containment

- Simplest sufficient form: a small C tool (`viz/contact_sheet.c`) that links the
  vendored `stb_image` / `stb_image_write` and `src/pep.h` + `src/pep_codec.h`,
  reads `performance-test/results.txt`, and emits one comparison PNG per image
  under `viz/shots/`. No browser, no network, works headless. (A web page is
  acceptable instead, but only if vendored locally with no runtime CDN and a
  headless screenshot driver — the C contact sheet is preferred for being
  trivially reproducible.)
- Build it as a separate target (e.g. `viz/Makefile` or a rule that does not
  touch the reference or optimized targets). It is never timed and never imported
  by the harness.

## Inspect the output (the verification step — not optional)

Generate the sheets, then **open the PNGs and look at them**: confirm the
quantized and round-trip panels are identical, the palette-size caption matches
the manifest, and the quantization loss looks reasonable for the stated color
count. Cover **every** committed image plus at least one synthetic low-color
image from `build/gen_images_seed` (where quantization is near-lossless and all
three panels should look essentially the same). Fix the tool when a panel is
wrong — do not adjust the artifact.

## Constraints

- **Never modify** the reference (`src/`, `test/`, `images/`, the reference
  harness) or the optimized compressor to suit the viewer.
- The decoded panels and `viz/shots/` are **regenerable** — `.gitignore` them.
  The committed artifact is the tool source and a short `viz/README.md` with run
  instructions.

## Acceptance checklist

- [ ] The tool renders, per image, original | quantized | PEP round-trip
      side-by-side with a caption (name, dims, original colors, palette size,
      source/.pep bytes, ratio, bpp).
- [ ] The quantized and round-trip panels are verified **pixel-identical** (PEP
      lossless); any divergence is reported, not hidden.
- [ ] Panels are labeled so PEP is not misrepresented as lossy (loss is from
      quantization).
- [ ] Output covers every committed image plus ≥1 synthetic low-color image; the
      PNGs were opened and confirmed correct.
- [ ] Self-contained: vendored libs only, no runtime CDN; never timed; never
      imported by the harness. `viz/shots/` git-ignored; `viz/README.md`
      documents how to run it.

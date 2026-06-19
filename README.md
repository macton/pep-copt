# PEP recompression — an honest ~2× compressor, and how an LLM got it there

This project recompresses ordinary images with **PEP** (Prediction-Encoded
Pixels), the palette codec from [ENDESGA/PEP](https://github.com/ENDESGA/PEP). It
contains two things: a faithful reference C implementation of the
load → quantize → compress → save pipeline ([`src/`](src/)), and an optimized
compressor ([`src-optimized/`](src-optimized/)) that, on a 24-image real-photo
benchmark, compresses **~2× faster** while its output stays **lossless, decodes
pixel-identical to the reference, is never a larger `.pep`, and is no slower to
decode**.

![original | quantized | PEP round-trip comparison](viz/shots/pdia-d4ef529d-8f02-4f06-9152-eb4261c1de9f.jpg.png)

The headline is **2.04× aggregate** (per-image ~1.5–2.6×) on the committed
24-image set, median-of-5, single thread, gcc/WSL2. I want to be honest about how
I got there, because an earlier version of this same project *looked* like 6–18×.
That number was an artifact: it came from a shortcut that shipped larger-than-
reference files on about a third of the images. A broader benchmark and a size
gate exposed it. The defensible number is ~2×, and the story below is mostly about
how the measurement, not the model, drove it there.

One thing up front: PEP is a **palette codec for low-color images**. It runs a
per-channel PPM model over a ≤256-color palette and picks the model that produces
the smallest output. It shines on pixel art and other low-color content. It does
*not* beat JPEG on photographs — photographic input here is quantized to ≤256
colors first, and the resulting `.pep` files are *larger* than the source JPEGs.
That is expected for a palette codec on photos. The point of this repo is the
optimization *method* and how I kept it honest, not beating JPEG.

## What this project is for

Two purposes:

1. **The actual optimized compression path** — a per-model encoder that is about
   twice as fast as the reference's, with the output held byte-identical to the
   reference's.
2. **A reproducible case study of using an LLM to do the optimization** — what I
   (the human) did, what the model did, and what kept it honest.

This is a case study, not a benchmark of models, and it ran across more than one
model. The sequence, honestly:

- **Qwen3.7-Plus** (via the Together provider) drove the byte-identical contract
  to a gate-confirmed **1.42×** (a hash-table palette/index fix) and then exited
  with "NO FURTHER OPPORTUNITIES." It judged the original 10× target unachievable
  under byte-identity and single-thread constraints: the arithmetic coder is ~97%
  of runtime and inherently sequential. Its encode-loop attempts either regressed
  or broke correctness — most notably a reciprocal-division change that was not
  bit-exact and caused infinite loops/segfaults, which was reverted.
- The run then **continued with a different model, GPT-5.5**. It reached **~2.12×**
  byte-identical, then **~8.7×** under a relaxed contract on the original 3-image
  set — until the benchmark was expanded to 24 real images and the size gate
  showed that ~8.7× was a size-breaking shortcut. The compressor was switched to
  **strict all-model selection**, landing the honest, locked **~2×**.

The locked, committed result is the strict ~2×. The interim 8.7× was not a real
win; do not read it as the result.

## The roles

| Role | What it did |
|---|---|
| **Me (human)** | Defined the problem and the output contract; tightened the measure when it was gamed; locked the honest result. |
| **The model(s)** | Generated and optimized the compressor from the instruction docs; proposed each hypothesis; kept the log. |
| **The test harness** | Ground truth: comparator, correctness suite, determinism, generalization, median-of-5 timing, plus the size and decode gates. |
| **The proof driver** | `prove-optimized-harness.sh` re-measures the whole thing per run, so claims come from fresh measurement, not the model's recollection. |

**Me (the human).** I defined the problem and the output contract. It started as
**byte-identical `.pep`** to the reference. I later **relaxed** it to
*pixel-identical after decompression* (the `.pep` format may change as long as it
decodes to the reference's exact pixels) to open headroom toward the 10× target.
Crucially, when the model gamed the looser measure, I **tightened** it: I added a
**size gate** and a **decode gate**, and I **expanded the benchmark from 3 to 24
real images**. That broader benchmark exposed the single-model shortcut and led me
to **lock the strict, never-larger ~2×** as the honest result. The original 10×
was a judgment call from reading the compressor, not a derived bound; once the data
showed the achievable speed is a *frontier set by the size contract*, I **reset**
it — there is no single finish-line number, only "fastest without shipping a larger
file." I wrote the instruction documents and decided what to keep.

**The model(s).** They generated and optimized the compressor from the instruction
documents, proposed and implemented each optimization hypothesis, and kept the log.
The handoff: Qwen3.7-Plus reached ~1.42× then stopped; GPT-5.5 continued, reaching
~2.12× byte-identical and ~8.7× under the relaxed contract — but that ~8.7× leaned
on the single-model shortcut and **did not survive the broader benchmark + size
gate**. The locked result is the strict ~2×.

**The test harness (ground truth).** The comparator, the correctness suite, the
determinism check, the synthetic-input generalization check, and the median-of-5
timing protocol. The **match contract** was relaxed mid-project. Phase A required
the optimized output to be **byte-identical** to the reference `.pep`. Phase B
relaxed that to **pixel-identical after decompression** — the format may change as
long as decompressing reproduces the reference's exact pixels (PEP stays lossless;
the comparator decompresses to compare, it never diffs the compressed bytes). Two
guard rails back the relaxed contract: the optimized `.pep` must be **no larger**
than the reference's (per image), and **decompression must be net-neutral-or-
better** (optimized decode `<=` reference decode within ~3%) — both gated, so a win
cannot be bought with a bigger file or a slower decoder. Determinism (same code →
same bytes every run) and lossless round-trip are required throughout.

**The agent harness.** Working state lives in inspectable files — the source under
`src-optimized/`, the log, the results manifests — and `prove-optimized-harness.sh`
is run per cycle to surface the real, measured gate status rather than the model's
recollection.

### Why the grounding matters

A model left to optimize on its own drifts: it reasons from recollection instead
of fresh measurement, loses uncommitted gains, and can report results it did not
run. Inspectable state, committing every kept gain, and a per-run proof convert
"the model says it is faster and correct" into "measured faster, gates pass,
committed." That is the whole reason the size gate caught the 8.7× shortcut instead
of me shipping it.

## Methodology — four documents, four phases

1. [`prompts/create-reference.md`](prompts/create-reference.md) → the reference
   pipeline in [`src/`](src/): load (stb_image) → deterministic median-cut
   quantize to ≤256 colors → `pep_compress` (the one timed call) → `pep_save` →
   lossless round-trip check. Single-threaded, deterministic, libc/libm + vendored
   single-headers only.
2. [`prompts/create-optimized-test-harness.md`](prompts/create-optimized-test-harness.md)
   → the comparator (decompressed-pixel match, relaxed from byte-identity), the
   generalization generator, the committed images, and the determinism check. It
   was built and **proven against an identity copy** (`src-optimized/` ==
   `src/`, ≈1.0×) *before* any optimization, as iteration-0 evidence the
   measurement pipeline is sound.
3. [`prompts/create-optimized.md`](prompts/create-optimized.md) → the optimization
   instructions: profile first, rank candidates by payoff, hold the pixel-identity
   gate plus the size and decode gates, treat representation/data-pattern changes
   and SIMD/layout as first-class.
4. [`prompts/create-visualizer.md`](prompts/create-visualizer.md) → the quality
   visualizer (its own section below).

## What was optimized — and what survived the strict measure

The optimization log
([`src-optimized/OPTIMIZATION-LOG.md`](src-optimized/OPTIMIZATION-LOG.md)) records
each hypothesis and a re-assessment of every prior win under the strict measure.
The wins that survive all make the *per-model* encode faster — and because the
reference also encodes per model, they win on every pass:

| Kept under strict (these ARE the ~2×) | What it does |
|---|---|
| Palette hash lookup | O(1) index build vs the reference's per-pixel linear palette scan |
| Block-prefix frequency sums (16-symbol blocks) | O(blocks) cumulative-frequency query vs a linear scan |
| Encoder model-kind specialization | straight-line per-kind hot path instead of generic dispatch |
| Encoder-only padded neighbor taps | drops boundary checks on the common path |
| Local arithmetic-coder state + escape fast path | branch/memory savings per symbol |
| **Early-abandon + count-only loser evaluation** | **measured +30% under strict** (1.57× → 2.04×): losing models stop early |

Together the per-symbol wins make the optimized per-model encode ~2× the
reference's; early-abandon is what keeps the 3-model exhaustive search viable.

What lost its value or was removed under strict:

- **Single-model heuristic — removed.** It was the ~4.5× of the old "9.63× / 6–18×"
  *and* the cause of the size regressions (it skipped model evaluation and shipped
  larger-than-reference files on ~1/3 of images, up to +35%). Fundamentally
  incompatible with "never larger."
- **Direct W/NW encoder — demoted.** Measured only ~+3.5% once all models are
  evaluated; kept but no longer a headline win.
- **Trial-order heuristic — demoted.** Under single-model it *selected* the model;
  under strict it only orders the early-abandon trials. Kept (cheap).

Rejected build-level levers (tried offline, while the model run was out of quota):
**PGO** (no gain — `-O3 -march=native` already optimizes the hot loop), **LTO**
(failed the decode gate — sped compress but slowed the optimized decoder
1.01×→1.24×), and **reciprocal division** for `range /= scale` (bit-exact but a
measured regression on this hardware; the integer divide is cheap here). A fused
3-model pass was measured and rejected too: the encode is compute-bound, not
memory-bound, so fusing the data reads saves ≤~4%.

The pattern: the wins that survive make the per-model encode faster; the ones that
vanished all relied on doing *less model work* — exactly what the size gate ruled
out.

## The visualizer

[`viz/`](viz/) is a small C tool (`viz/contact_sheet.c`) that renders, one image
at a time, a side-by-side comparison: the **original** photo, the **quantized**
version (≤256 colors, what PEP actually compresses), and the **PEP round-trip**
(the decompressed `.pep`). PEP is lossless on palettized pixels, so the quantized
and round-trip panels are pixel-identical — all visible loss is from quantization,
not from PEP. The screenshots in [`viz/shots/`](viz/shots/) came from this tool.
Regenerate them with:

```sh
make all && make input && ./run-performance-test   # produce results.txt + .pep
make -C viz shots                                   # write viz/shots/*.png
```

![pdia-2386a949 original | quantized | round-trip](viz/shots/pdia-2386a949-c8c8-4512-b39e-916c2ba319bb.jpg.png)
![pdia-ce076845 original | quantized | round-trip](viz/shots/pdia-ce076845-ab86-48f5-a7df-f2997d9746bb.jpg.png)

## Honest results and limits

The locked, gate-passing result on the 24-image benchmark
([`performance-test-optimized/HARNESS-BASELINE.md`](performance-test-optimized/HARNESS-BASELINE.md),
[`performance-test-optimized/proof-run.log`](performance-test-optimized/proof-run.log)):

```
reference median:  17.457696 s   (compression only)
optimized median:   8.563426 s   (compression only)
speedup:            2.04x        (per-image ~1.5-2.6x; strict never-larger)
comparator:         PASS  (pixel-identical, 0 round-trip failures, 0 size regressions)
opt .pep vs ref:    1.0000x      (byte-identical)
decode:             opt/ref 1.01x (net-neutral-or-better)
tests:              13/13  | determinism: byte-identical | generalization: PASS
```

Conditions: committed images, median-of-5, single thread, gcc/WSL2. 10× was not
reached, and it is no longer the goal.

**The frontier** (measured, from the log) is the honest framing of "how fast":

| approach | speed | size regressions | worst |
|---|--:|--:|--:|
| **strict exhaustive (LOCKED)** | **2.04×** | **0/24** | — |
| sample-band H/4 selection | 3.16× | 8/24 | +8% |
| sample-band H/16 selection | 5.43× | 10/24 | +12% |
| single-model heuristic | 9.25× | 8/24 | +35% |

So: strict "never larger" ≈ 2×; ~5× only if you allow `.pep` up to ~1.13× of
reference; ~9× only if you abandon the size guarantee. The gates earned that
honesty by **rejecting** real attempts: a ~50× raw-index path that inflated files
~3.65× (size gate), a ~21% decode regression (decode gate), and — the big one — the
~8.7× that **failed on the 24-image benchmark** because it shipped larger files on
~1/3 of images via the single-model shortcut.

Generalization: two synthetic low-color seed sets both pass pixel-identical (the
2× win does not register on tiny 256×256 inputs, but correctness and the size/
decode bounds hold). Results are machine-specific and single-threaded. Note again
that on these photographs the `.pep` files are larger than the source JPEGs —
expected for a palette codec on photos, not a defect of the optimization.

### Per-image examples

A representative handful of the 24 committed images, largest to smallest. The
optimized `.pep` is byte-identical to the reference's in every case (size ratio
1.00×), so the speedup is a genuine win, not bytes traded for time. `.pep` sizes
are from the two `results.txt` files; source JPEG bytes from `stat`; compress times
are per-image median-of-5.

#### pdia-d4ef529d — 3928×1724
![pdia-d4ef529d comparison](viz/shots/pdia-d4ef529d-8f02-4f06-9152-eb4261c1de9f.jpg.png)

| metric | reference | optimized |
|---|--:|--:|
| source JPEG | 1,733,255 B | — |
| `.pep` size | 4,232,233 B | 4,232,233 B (1.00×, byte-identical) |
| compress time | 2.082 s | 1.059 s |
| speedup | — | ~1.97× |

#### pdia-2386a949 — 3366×2099
![pdia-2386a949 comparison](viz/shots/pdia-2386a949-c8c8-4512-b39e-916c2ba319bb.jpg.png)

| metric | reference | optimized |
|---|--:|--:|
| source JPEG | 2,008,953 B | — |
| `.pep` size | 4,029,204 B | 4,029,204 B (1.00×, byte-identical) |
| compress time | 2.138 s | 1.039 s |
| speedup | — | ~2.06× |

#### pdia-ce076845 — 1000×1444
![pdia-ce076845 comparison](viz/shots/pdia-ce076845-ab86-48f5-a7df-f2997d9746bb.jpg.png)

| metric | reference | optimized |
|---|--:|--:|
| source JPEG | 601,697 B | — |
| `.pep` size | 1,010,065 B | 1,010,065 B (1.00×, byte-identical) |
| compress time | 0.604 s | 0.328 s |
| speedup | — | ~1.84× |

#### pdia-5d46dfcd — 832×1200
![pdia-5d46dfcd comparison](viz/shots/pdia-5d46dfcd-8d1a-4803-943b-1ee67cb08f08.jpg.png)

| metric | reference | optimized |
|---|--:|--:|
| source JPEG | 467,724 B | — |
| `.pep` size | 742,410 B | 742,410 B (1.00×, byte-identical) |
| compress time | 0.439 s | 0.255 s |
| speedup | — | ~1.72× |

#### pdia-07549118 — 553×800
![pdia-07549118 comparison](viz/shots/pdia-07549118-a593-4e0e-8aa1-6a9ae1e626f3.jpg.png)

| metric | reference | optimized |
|---|--:|--:|
| source JPEG | 117,663 B | — |
| `.pep` size | 211,793 B | 211,793 B (1.00×, byte-identical) |
| compress time | 0.183 s | 0.118 s |
| speedup | — | ~1.56× |

Across all 24 images the harness reports the aggregate: reference median ~17.5 s →
optimized median ~8.6 s, **2.04×**, with per-image speedups ~1.5–2.6×. The
optimized `.pep` is byte-identical to the reference (never larger), so the ~2× is a
real per-model encode win.

## Reproduce it yourself

```sh
# Reference: build, quantize the committed images, run the reference harness
make clean && make all
make input
./run-performance-test                       # writes performance-test/results.txt

# Optimized: build and run the full correctness suite against the optimized lib
make -f Makefile.optimized optimized
./build/test_runner_opt                      # 13/13
./run-performance-test-optimized             # writes performance-test-optimized/results.txt

# The reference test suite
make test                                    # ./build/test_runner

# The full per-run proof (clean build, all gates, median-of-5; takes a few minutes)
./prove-optimized-harness.sh

# The visualizer
make -C viz shots                            # writes viz/shots/*.png
```

## Citation and license

PEP is by **ENDESGA** ([github.com/ENDESGA/PEP](https://github.com/ENDESGA/PEP)),
released under **CC0 1.0**. The vendored `stb` single-header libraries
(`stb_image.h`, `stb_image_write.h`, `stb_easy_font.h`, from
[nothings/stb](https://github.com/nothings/stb)) carry their own public-domain/MIT
terms.

The project as a whole is **MIT** ([`LICENSE`](LICENSE)) — that covers the harness,
prompts, tests, tooling, and build scripts. The image codec sources under
[`src/`](src/) and [`src-optimized/`](src-optimized/) are dedicated to the public
domain under **CC0 1.0 Universal** ([`src/LICENSE`](src/LICENSE),
[`src-optimized/LICENSE`](src-optimized/LICENSE)) to match the upstream PEP project
(CC0 1.0) that `pep.h` derives from. No terms here go beyond what those files state.

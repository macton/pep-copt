# create-readme.md — instructions to generate the root README.md

You are writing `README.md` in the root of this project. This document tells you
what it must contain, in what order, in whose voice, and — most importantly —
which facts to ground in the actual repository instead of inventing.

The project recompresses ordinary images with **PEP** (Prediction-Encoded
Pixels) and contains (a) a faithful reference C implementation of the
load→quantize→compress→save pipeline and (b) an optimized compressor that, on a
**24-image benchmark**, is **~2× faster** at compression (per-image ~1.5–2.6×)
while its output is **lossless, decodes pixel-identical to the reference, is
never larger than the reference's `.pep`, and decodes no slower**.

Keep the README to context and results. Do not narrate the project's history,
the measurement process, or the changes of contract along the way — that detail
lives in `OPTIMIZATION-LOG.md`. State what the thing is and what it does now.

Ground every number in the repo; report whatever `./prove-optimized-harness.sh`
and `OPTIMIZATION-LOG.md` currently confirm. The headline is **~2×** (aggregate;
per-image ~1.5–2.6×).

## Voice and tone

First person, as **Mike Acton** ("I decided…", "I wrote…", "My target was…").
Straightforward, plain, technical. State things directly. Not snarky, not
dismissive — not about the model, the PEP author, or anyone's code. No marketing
hype and no false modesty. Short declarative sentences. No exclamation points, no
"amazing"/"incredible", no emoji. When something is uncertain or unverified, say
so. Do not use the word "honest" or its variants; just state the result.

## Ground every claim — do not fabricate

Every number, filename, and behavioral claim must come from the repository.

- **Read these before writing:**
  - `https://github.com/ENDESGA/PEP` — the PEP format. Read enough to describe
    what the compressor computes (per-channel PPM over a ≤256-color palette,
    model-selected for smallest output) and where it shines (low-color pixel
    art) versus where it does not (photographs, which must be quantized first and
    where JPEG's DCT still wins on size).
  - `src/README.md` and `src/pep_codec.{h,c}`, `src/pep.{c,h}` — the reference.
  - `prompts/create-reference.md`, `prompts/create-optimized-test-harness.md`,
    `prompts/create-optimized.md`, `prompts/create-visualizer.md` — the
    instruction documents that drove each phase. Read each so you describe it
    accurately.
  - `src-optimized/OPTIMIZATION-LOG.md` — the per-hypothesis history (kept and
    rejected). Source for the "what was optimized" section.
  - `prove-optimized-harness.sh` and
    `performance-test-optimized/HARNESS-BASELINE.md` — the proof harness and its
    baseline.
- **The headline speedup is a measured value.** Obtain it by running
  `./prove-optimized-harness.sh` (its FINAL SUMMARY prints the median-of-5
  compression speedup and the gate verdicts) or, if you cannot run it, from the
  latest gate-passing figure in `OPTIMIZATION-LOG.md`. State it as a multiple
  and always state the conditions: committed images, median-of-5, single thread,
  the machine (gcc/WSL2). Do **not** hard-code a number you did not verify.
- If a fact is not in the repo, leave it out. Never invent measurements, dates,
  or quotes.

## Required structure

1. **Title + lede.** A simple, direct title naming the result (e.g. "PEP
   compression performance improvement: ~1.5–2.6×"). Then a short lede: name PEP
   and the repo it comes from, state this repo has a faithful reference
   implementation and an optimized compressor that is **~2× faster** at
   compression (24-image benchmark, median-of-5, single thread, gcc/WSL2) while
   its output is **lossless, pixel-identical to the reference once decompressed,
   never a larger `.pep`, and no slower to decode**. Use whatever the current
   harness confirms for the exact figure. Embed one representative
   `viz/shots/*.png` so a reader sees the original|quantized|round-trip
   comparison immediately. State plainly that PEP is a **palette codec for
   low-color images**: photographic input is quantized to ≤256 colors first, and
   on photos PEP is not smaller than JPEG. Keep the lede to context and results
   only — no history, no measurement narrative.

2. **What this project is for.** Two purposes, stated as two points and nothing
   more:
   1. The actual optimized compression path (a per-model encoder ~2× faster than
      the reference, output byte-identical to the reference).
   2. A reproducible case study of using an LLM to do the optimization — what the
      human did, what the model did, and what the harness verified.

3. **Methodology — four documents, four phases.** One phase per instruction
   document, each linked, 2–4 sentences:
   1. [`prompts/create-reference.md`](prompts/create-reference.md) → the
      reference pipeline in [`src/`](src/).
   2. [`prompts/create-optimized-test-harness.md`](prompts/create-optimized-test-harness.md)
      → the comparator (decompressed-pixel match, relaxed from byte-identity), the
      generalization generator, the committed images, the determinism check.
      Note it was built and proven against an identity copy **before** any
      optimization. Mention the two guard rails the harness enforces: the
      optimized `.pep` must be **no larger** than the reference's (per image), and
      **decompression must be net-neutral-or-better**.
   3. [`prompts/create-optimized.md`](prompts/create-optimized.md) → the
      optimization instructions (profile first, rank by payoff, the
      pixel-identity gate plus the size and decode gates, SIMD/layout and
      data-pattern changes as first-class).
   4. [`prompts/create-visualizer.md`](prompts/create-visualizer.md) → the
      quality visualizer (its own section).

4. **What was optimized.** Mine `OPTIMIZATION-LOG.md`. Do not paste it. Give a
   short intro sentence and a single table of the **kept** optimizations that
   carry the ~2× (the per-model encoder speedups — palette hash lookup,
   block-prefix frequency sums, model-kind specialization, padded neighbor taps,
   local coder state — plus early-abandon + count-only loser evaluation). Point to
   `OPTIMIZATION-LOG.md` for the full per-hypothesis history, rejected attempts,
   and the size/speed frontier. Do not reproduce the rejected list or the frontier
   prose here.

5. **The visualizer.** Describe `viz/` per `create-visualizer.md`: the
   original|quantized|round-trip comparison, how to run it, and that the
   screenshots in `viz/shots/` came from it. Embed two or three. Use real paths
   that exist.

6. **Per-image results (one block per image — all 24).** Give **every committed
   image** its own block, largest to smallest. Do not pick a subset and do not
   use a single combined table. For each, show **the image** then its
   **before/after** data underneath. Each block:

   - Embed `viz/shots/<name>.png` (original | quantized | PEP round-trip).
   - Underneath, a small table for that one image: dimensions, source JPEG bytes,
     reference vs optimized `.pep` bytes (**equal — strict selection makes the
     optimized output byte-identical to the reference**, ratio 1.00×), reference
     vs optimized **compress time**, and the per-image speedup.

   **Ground every value** — re-derive from the current repo; do not copy a
   snapshot. How to source each:
   - **dimensions + `.pep` bytes**: the `w h … pep_bytes` columns of
     `performance-test/results.txt` (reference) and
     `performance-test-optimized/results.txt` (optimized) — they match.
   - **source JPEG bytes**: `stat -c '%s' images/<name>`.
   - **per-image compression time** (the harness reports a per-run *total*, so
     time one image at a time): `echo build/q/<name>.qbin > /tmp/one.list &&
     performance-test-optimized/measure-speedup.sh ./build/perf_harness /tmp/one.list /tmp/o`
     (and again with `./build/perf_harness_opt`); read the `median:` line.

   Block format:

   ```markdown
   #### pdia-d4ef529d… — 3928×1724
   ![pdia-d4ef529d comparison](viz/shots/pdia-d4ef529d-8f02-4f06-9152-eb4261c1de9f.jpg.png)

   | metric | reference | optimized |
   |---|--:|--:|
   | source JPEG | 1,733,255 B | — |
   | `.pep` size | 4,232,233 B | 4,232,233 B (1.00×, byte-identical) |
   | compress time | _measure_ s | _measure_ s |
   | speedup | — | ~_measure_× |
   ```

   After the blocks, state the aggregate the harness reports (median total
   compression over all 24: ~17.5 s reference → ~8.6 s optimized, **~2.04×**;
   per-image ~1.5–2.6×).

7. **Reproduce it yourself.** Concise, copy-pasteable: build (`make clean &&
   make`, `make -f Makefile.optimized optimized`), run the tests
   (`make test`, `./build/test_runner_opt`), `./run-performance-test` /
   `./prove-optimized-harness.sh`, and the visualizer. Pull exact commands from
   the scripts; verify they are current.

8. **Citation and license.** Credit PEP (ENDESGA, CC0-1.0) and the vendored stb
   libraries; point at their sources. End the README with a **short license
   note** (a few sentences, no legalese) that clarifies the split, grounded in
   the actual files: the project as a whole is **MIT** (`LICENSE` at the root —
   covers the harness, prompts, tests, tooling, and build scripts), while the
   image codec sources under [`src/`](src/) and [`src-optimized/`](src-optimized/)
   are dedicated to the public domain under **CC0 1.0 Universal**
   (`src/LICENSE`, `src-optimized/LICENSE`) to match the upstream PEP project
   (https://github.com/ENDESGA/PEP, CC0 1.0) that `pep.h` derives from. Note the
   vendored stb headers carry their own public-domain/MIT terms. Do not invent
   terms beyond what those files state.

## Screenshots

At least one in the lede, two or three in the visualizer section. Reference only
files that actually exist under `viz/shots/`. Plain Markdown image syntax with
short, accurate alt text.

## Length and format

GitHub-flavored Markdown. Headings, a little bulleting, the kept-optimizations
table and the per-image blocks. Commands in fenced blocks. No badge spam.

## Final self-check before you finish

- [ ] The title and lede are simple and direct (result + conditions); a
      screenshot is visible immediately; PEP's palette/low-color nature is stated;
      the lede is context and results only, with no history or process narrative.
- [ ] The word "honest" (and its variants) does not appear anywhere.
- [ ] "What this project is for" is exactly the two points — nothing else.
- [ ] No "roles" section and no "why grounding matters" section.
- [ ] "What was optimized" is an intro sentence plus the kept-optimizations table
      only, pointing to `OPTIMIZATION-LOG.md` for the rest.
- [ ] The headline is the **strict ~2×** (per-image ~1.5–2.6×), measured, with
      stated conditions; nothing fabricated.
- [ ] Per-image results show **one block per image for all 24** committed images
      (image embedded; before/after `.pep` size — equal/byte-identical —
      compression time, and per-image speedup underneath) — not a combined table,
      not a subset — every value re-derived from the current repo.
- [ ] The four `prompts/*.md` documents are each linked and accurately described.
- [ ] Kept optimizations summarized from the log (not pasted); no repeated
      justification of why the speedup is real.
- [ ] Build/run/visualizer commands present and verified against the repo.
- [ ] The closing license note states the MIT-whole / CC0-codec split and the
      upstream PEP (CC0) mapping, grounded in the actual LICENSE files.
- [ ] First person, Mike Acton, straightforward, not snarky.

# create-readme.md — instructions to generate the root README.md

You are writing `README.md` in the root of this project. This document tells you
what it must contain, in what order, in whose voice, and — most importantly —
which facts to ground in the actual repository instead of inventing.

The project recompresses ordinary images with **PEP** (Prediction-Encoded
Pixels) and contains (a) a faithful reference C implementation of the
load→quantize→compress→save pipeline and (b) an optimized compressor that, on a
**24-image real-photo benchmark**, is **~2× faster** at compression while its
output is **lossless, decodes pixel-identical to the reference, is never larger
than the reference's `.pep`, and decodes no slower**. You are writing the story of
how that honest ~2× was reached — and, just as importantly, how the harness
exposed and rejected several *apparent* (but dishonest) larger speedups along the
way.

Ground every number in the repo; report whatever `./prove-optimized-harness.sh`
and `OPTIMIZATION-LOG.md` currently confirm. The headline is **~2×**, but the
real story is the arc that got there (describe it plainly):

- **Byte-identical pass.** First contract: optimized `.pep` byte-for-byte
  identical to the reference's. GPT-5.5 reached **~2.12×** and judged 10×
  structurally unreachable (the arithmetic coder is ~97% of runtime and inherently
  sequential). (Earlier, Qwen3.7-Plus had taken it to ~1.42× and stopped.)
- **Relaxed contract.** The match rule was deliberately loosened to
  *pixel-identical after decompression* (the `.pep` format may change, as long as
  it decodes to the reference's exact pixels) to open headroom toward 10×. On the
  original **3-image** set this reached a gate-confirmed **~8.7×** — but only after
  two **guard rails** had to be added when passes *gamed* the loophole: a **size
  gate** (a pass emitted ~3.65× **larger** files for a meaningless ~50×) and a
  **decode gate** (a pass **slowed decompression ~21%** to speed compression).
- **The reckoning (current, locked).** Expanding the benchmark to **24 real
  images** made the size gate fire: that ~8.7× depended on a **single-model
  shortcut** — testing only 1 of the reference's 3 prediction models — which
  shipped **larger-than-reference files on ~1/3 of the images (up to +35%)**.
  Switching to **strict all-model selection** (always keep the smallest, exactly
  like the reference) makes the output byte-identical and never-larger, at the
  honest cost of the shortcut: **~2×**. That 2× is the genuine per-model encoder
  speedup; **the 6–18× / 8.7× was the shortcut, not a real win.**

The takeaway the README should land: the impressive numbers were an artifact of a
weak benchmark and a missing gate; a broader benchmark plus size/decode gates
drove the result to an honest, defensible **~2× that is lossless and never
larger**.

## Voice and tone

First person, as **Mike Acton** ("I decided…", "I wrote…", "My target was…").
Straightforward, plain, technical. State things directly. Not snarky, not
dismissive — not about the model, the PEP author, or anyone's code. No marketing
hype and no false modesty. Short declarative sentences. No exclamation points, no
"amazing"/"incredible", no emoji. When something is uncertain or unverified, say
so.

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
  (e.g. "~8×") and always state the conditions: committed images, median-of-5,
  single thread, the machine (gcc/WSL2). Do **not** hard-code a number you did
  not verify.
- If a fact is not in the repo, leave it out. Never invent measurements, dates,
  or quotes.

## Required structure

1. **Title + lede.** Name PEP and the paper/repo it comes from; state this repo
   has a faithful reference implementation and an optimized compressor that is
   **~2× faster** at compression (24-image benchmark, median-of-5, single thread,
   gcc/WSL2) while its output is **lossless, pixel-identical to the reference once
   decompressed, never a larger `.pep`, and no slower to decode**. Use whatever
   the current harness confirms for the exact figure. Make the honesty the hook:
   note in one line that earlier passes *looked* like 6–18× but that was a
   benchmark/shortcut artifact the size gate later exposed — the defensible number
   is ~2×. Embed one representative `viz/shots/*.png` so a reader sees the
   original|quantized|round-trip comparison immediately. State plainly that PEP
   is a **palette codec for low-color images**: photographic input is quantized
   to ≤256 colors first, and on photos PEP is not smaller than JPEG — the point
   here is the optimization *method* (and how it was kept honest), not beating JPEG.

2. **What this project is for.** Two purposes: (1) the actual optimized
   compression path, and (2) a concrete, reproducible demonstration of how an LLM
   was used to do the optimization — what the human did, what the model did, and
   what kept it honest. Name the model(s) early. This is a case study, not a
   benchmark of models, and it ran across **more than one model** — describe the
   sequence honestly:
   - **Qwen3.7-Plus** (via the Together provider) drove the optimization to a
     gate-confirmed **1.42×** (the hash-table palette/index fix) and then exited
     with **"NO FURTHER OPPORTUNITIES"** at that point: it judged the 10× target
     unachievable under the byte-identity and single-thread constraints (the
     arithmetic coder is ~97% of runtime and inherently sequential), after its
     encode-loop attempts either regressed or broke correctness — most notably a
     reciprocal-division change that was not bit-exact and caused infinite
     loops/segfaults, which was reverted.
   - The run then **continued with a different model, GPT-5.5**, which reached
     ~2.12× byte-identical, then ~8.7× under the relaxed contract on the 3-image
     set — until the benchmark was expanded to 24 real images and the size gate
     showed that ~8.7× was a size-breaking shortcut. The compressor was switched
     to **strict all-model selection**, landing the honest, locked **~2×**.
   Report whatever the final committed state actually shows (the strict ~2×); do
   not present the interim 8.7× as the result.

3. **The roles (the heart of the README).** Four clear roles:
   - **Me (the human).** Defined the problem and the output contract — initially
     **byte-identical `.pep`**, then **relaxed to pixel-identical after
     decompression** to open headroom. Crucially, also **tightened** the measure
     when the model gamed it: added the **size gate** and **decode gate**, and
     **expanded the benchmark from 3 to 24 real images** — which exposed the
     single-model shortcut and led me to **lock the strict, never-larger ~2×** as
     the honest result. Set the original 10× target (a judgment call from reading
     the compressor, not a derived bound) and then **reset it** once the data
     showed the achievable speed is a frontier set by the size contract, not a
     single number. Wrote the instruction documents; decided what to keep.
   - **The model(s).** Generated/optimized the compressor from the instruction
     documents; proposed and implemented each optimization hypothesis; kept the
     log. State the handoff: **Qwen3.7-Plus** (Together) reached ~1.42× then
     stopped; **GPT-5.5** continued, reaching ~2.12× byte-identical and ~8.7×
     under the relaxed contract — but that ~8.7× leaned on the single-model
     shortcut and **did not survive the broader benchmark + size gate**. The
     locked result is the **strict ~2×**. Attribute kept/rejected optimizations to
     whichever model produced them where the log makes that clear, and be clear
     which "wins" turned out to be shortcut artifacts.
   - **The test harness (ground truth).** The comparator, the correctness suite,
     the determinism check, the synthetic-input generalization check, and the
     median-of-5 timing protocol. Explain the **match contract** and that it was
     relaxed mid-project: Phase A required the optimized output to be
     **byte-identical** to the reference `.pep`; Phase B relaxed that to
     **pixel-identical after decompression** — the `.pep` format may change as
     long as decompressing it reproduces the reference's exact pixels (PEP stays
     lossless; the comparator decompresses to compare rather than diffing the
     compressed bytes). Two guard rails back the relaxed contract: the optimized
     `.pep` must be **no larger** than the reference's (per image), and
     **decompression must be net-neutral-or-better** (optimized decode `<=`
     reference decode) — both gated by the harness so a win can't be bought with a
     bigger file or a slower decoder. Determinism (same code → same bytes every
     run) and lossless round-trip are required throughout.
   - **The agent harness** (if one was used). Describe how working state lives in
     inspectable files and how `prove-optimized-harness.sh` is run to surface the
     real, measured gate status rather than the model's recollection. Link it if
     it has a public home; omit if not applicable.

3a. **Why the grounding matters (short).** A model left to optimize on its own
   drifts: it reasons from recollection instead of fresh measurement, loses
   uncommitted gains, and can report results it did not run. Inspectable state,
   committing every kept gain, and a per-run proof convert "the model says it is
   faster and correct" into "measured faster, gates pass, committed." State it
   plainly; do not recount incidents or assign blame.

4. **Methodology — four documents, four phases.** One phase per instruction
   document, each linked, 2–4 sentences:
   1. [`prompts/create-reference.md`](prompts/create-reference.md) → the
      reference pipeline in [`src/`](src/).
   2. [`prompts/create-optimized-test-harness.md`](prompts/create-optimized-test-harness.md)
      → the comparator (decompressed-pixel match, relaxed from byte-identity), the
      generalization generator, the committed images, the determinism check.
      Emphasize it was built and proven against an identity copy **before** any
      optimization.
   3. [`prompts/create-optimized.md`](prompts/create-optimized.md) → the
      optimization instructions (profile first, rank by payoff, the
      pixel-identity gate, simplification pass, SIMD/layout as first-class).
   4. [`prompts/create-visualizer.md`](prompts/create-visualizer.md) → the
      quality visualizer (its own section).

5. **What was optimized — and what survived the strict measure.** Mine
   `OPTIMIZATION-LOG.md` (it has a "Review of prior optimizations under the strict
   measure" section — use it). Do not paste it. Give a readable table of the
   **optimizations that still carry the ~2×** — the per-*model* encoder speedups
   (palette hash lookup, block-prefix frequency sums, model-kind specialization,
   padded neighbor taps, local coder state) plus **early-abandon + count-only
   loser evaluation** (measured +30% under strict). Then a shorter list of what
   **lost its value or was removed** under strict — the **single-model heuristic**
   (removed; it was the source of the fake 6–18× *and* the size regressions), the
   **direct W/NW encoder** (measured only ~+3.5% once all models are evaluated),
   the **trial-order heuristic** (now only orders early-abandon). And the
   **rejected build-level levers**: PGO (no gain), LTO (failed the decode gate),
   reciprocal division (regression on this hardware). The takeaway: the wins that
   survive make the per-model encode faster; the ones that vanished all relied on
   doing *less* model work — exactly what the size gate ruled out.

6. **The visualizer.** Describe `viz/` per `create-visualizer.md`: the
   original|quantized|round-trip comparison, how to run it, and that the
   screenshots in `viz/shots/` came from it. Embed two or three. Use real paths
   that exist.

7. **Honest results and limits.** Lead with the **locked strict result: ~2×**
   (aggregate; per-image ~1.5–2.6×) on the 24-image benchmark, all gates green —
   pixel-identical, lossless, deterministic, **`.pep` never larger** (byte-
   identical to the reference), **decode net-neutral**. Use whatever the harness
   currently confirms; state conditions (median-of-5, single thread, gcc/WSL2).
   Then give the **frontier** honestly (from the log): strict "never larger" ≈ 2×;
   ~5× only if you allow `.pep` up to ~1.13×; ~9× only if you abandon the size
   guarantee. Explain that the gates earned that honesty by **rejecting** real
   attempts: a ~50× that inflated files (size gate), a ~21% decode regression
   (decode gate), and — the big one — the ~8.7× that **failed on a 24-image
   benchmark** because it shipped larger files on ~1/3 of images via a single-
   model shortcut. Be explicit about generalization (synthetic + the 24-image
   real set). Results are machine-specific and single-threaded. Note that on these
   photographs the `.pep` files are larger than the source JPEGs — expected for a
   palette codec on photos, not a defect of the optimization.

7a. **Per-image examples (required — one block per image, not a combined
   table).** The benchmark is now **24 images**; pick a **representative handful
   (≈3–6)** — a mix of sizes — and give each its own block (don't dump all 24).
   For each, show **the image itself** then its **before/after** data underneath.
   Each block:

   - Embed `viz/shots/<name>.png` (original | quantized | PEP round-trip) so the
     picture and its result sit together.
   - Underneath, a small **before/after** table for that one image: dimensions,
     source JPEG bytes, reference vs optimized `.pep` bytes (**these are equal —
     strict selection makes the optimized output byte-identical to the reference**,
     so the size ratio is 1.00×), reference vs optimized **compress time**, and the
     per-image speedup (~1.5–2.6×).

   **Ground every value** — re-derive from the current repo; do not copy the
   snapshot. How to source each:
   - **dimensions + `.pep` bytes**: the `w h … pep_bytes` columns of
     `performance-test/results.txt` (reference) and
     `performance-test-optimized/results.txt` (optimized) — they will match.
   - **source JPEG bytes**: `stat -c '%s' images/<name>`.
   - **per-image compression time** (the harness reports a per-run *total*, so
     time one image at a time): `echo build/q/<name>.qbin > /tmp/one.list &&
     performance-test-optimized/measure-speedup.sh ./build/perf_harness /tmp/one.list /tmp/o`
     (and again with `./build/perf_harness_opt`); read the `median:` line.

   Snapshot to refresh (real `.pep` sizes from the latest run; opt == ref
   byte-for-byte; fill the time fields from the per-image runs above). Example
   block:

   ```markdown
   ### pdia-d4ef529d… — 3928×1724
   ![pdia-d4ef529d comparison](viz/shots/pdia-d4ef529d-8f02-4f06-9152-eb4261c1de9f.jpg.png)

   | metric | reference | optimized |
   |---|--:|--:|
   | `.pep` size | 4,232,233 B | 4,232,233 B (1.00×, byte-identical) |
   | compress time | _measure_ | _measure_ |
   | speedup | — | ~2× (_measure_) |
   ```

   Use real current images for the other blocks (e.g. `pdia-2386a949…` 3366×2099,
   `pdia-5d46dfcd…` 832×1200, `pdia-ce076845…` 1000×1444 — all still committed).
   After the blocks, state the aggregate the harness reports (median total
   compression over all 24: latest ~17.5s reference → ~8.6s optimized, **~2.04×**;
   per-image ~1.5–2.6×). Keep the point visible: the optimized `.pep` is
   byte-identical to the reference (never larger), so the ~2× is a genuine win, not
   bytes traded for time.

8. **Reproduce it yourself.** Concise, copy-pasteable: build (`make clean &&
   make`, `make -f Makefile.optimized optimized`), run the tests
   (`make test`, `./build/test_runner_opt`), `./run-performance-test` /
   `./prove-optimized-harness.sh`, and the visualizer. Pull exact commands from
   the scripts; verify they are current.

9. **Citation and license.** Credit PEP (ENDESGA, CC0-1.0) and the vendored stb
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

Roughly 150–300 lines of GitHub-flavored Markdown. Headings, a little bulleting,
one or two small tables (the roles; kept/rejected optimizations). Commands in
fenced blocks. No badge spam.

## Final self-check before you finish

- [ ] The lede names PEP, the reference, and the **measured** speedup with its
      conditions; a screenshot is visible immediately; PEP's palette/low-color
      nature is stated.
- [ ] The model handoff is named early — Qwen3.7-Plus (~1.42×) → GPT-5.5 (~2.12×
      byte-identical, ~8.7× relaxed) → **strict ~2× after the 24-image benchmark +
      size gate exposed the shortcut** — and the four roles are clear, including
      what the per-run proof does.
- [ ] The headline is the **strict ~2×**; the 6–18× / 8.7× is described as a
      shortcut/benchmark artifact the gates exposed, not presented as the result.
- [ ] The original 10× target is explained as a judgment call (not a derived
      bound) AND noted as **reset** to the measured size-vs-speed frontier.
- [ ] Every number came from the harness or the log, with stated conditions;
      nothing fabricated; generalization limits stated.
- [ ] Per-image examples are shown as **one block per image** (image embedded;
      before/after `.pep` size — equal/byte-identical — compression time, and
      per-image speedup underneath) for a **representative handful of the 24**
      committed images — not a combined table — every value re-derived from the
      current repo.
- [ ] The four `prompts/*.md` documents are each linked and accurately described.
- [ ] Kept and rejected optimizations summarized from the log (not pasted).
- [ ] The full arc is explained: byte-identical (~2.12×) → relaxed
      pixel-identical-after-decompress (with the size + decode guard rails) →
      **strict all-model selection (~2×)** once the 24-image benchmark exposed the
      single-model shortcut. Lossless, deterministic, never-larger throughout; the
      locked headline is the strict ~2×.
- [ ] Build/run/visualizer commands present and verified against the repo.
- [ ] The closing license note states the MIT-whole / CC0-codec split and the
      upstream PEP (CC0) mapping, grounded in the actual LICENSE files.
- [ ] First person, Mike Acton, straightforward, not snarky.

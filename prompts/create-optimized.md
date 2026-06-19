# PEP Image Recompression — Optimized Implementation

Instructions for an LLM agent. You must **implement, build, test, measure, and
iterate** until the performance target is reached or you can prove no further
optimization opportunities remain. "Done" means one of the two exit criteria at
the bottom is met **with real command output as evidence**. Never report a step
as complete without having run it; never fabricate results or measurements.

## Context

A working reference implementation already exists, and so does the full **test,
comparison, and measurement harness** you will use to prove your work. Before
writing any code:

- Read `create-reference.md` and `../src/README.md` for the reference's pipeline
  and layout.
- Read the reference compressor `../src/pep.h` / `../src/pep.c`, the shared
  pre-processing library `../src/pep_codec.{h,c}`, the test suite
  `../test/test_main.c`, and the harness `../performance-test/`.
- Read `../performance-test-optimized/HARNESS-BASELINE.md` — the
  test/comparison/measurement scaffold is **already built and proven** against an
  identity copy (iteration-0 evidence the measurement pipeline is sound). You
  **reuse** it; you do not rebuild it.
- Read the PEP project page `https://github.com/ENDESGA/PEP` to understand what
  the compressor computes (a per-channel Prediction-by-Partial-Matching model
  over palette indices, model-selected for smallest output).

Reference baseline: whatever you **measure** on this machine with the protocol,
not a quoted number.

## The test harness already exists — use it, do not rebuild it

`src-optimized/pep.{c,h}` already exists as a **verbatim copy of `src/`** — that
copy is your starting point. **Your job is to edit `src-optimized/pep.h` (the
compressor), plus only the build wiring your change actually needs. Do not
recreate, weaken, or fork any harness piece.** Reuse these exactly:

- `run-performance-test-optimized` — quantizes the committed images (excluded
  from timing), runs the optimized harness, checksum-verifies `images/`, writes
  `performance-test-optimized/results.txt`.
- `build/compare_results <ref-results> <opt-results>` — the **decompressed-pixel
  comparator** (the optimized output, once **decompressed**, must be
  pixel-identical to the reference's decompressed output + lossless round-trip on
  both sides). It compares the decompressed-pixel digests, **not** the `.pep`
  bytes. Prints a fixed parseable report and exits 0 only on PASS. **This is the
  matcher; never substitute a bare `diff` of the manifests.**
- `performance-test-optimized/measure-speedup.sh` — the median-of-5 protocol.
- `build/test_runner_opt` — the **full reference correctness suite** against the
  optimized library.
- `build/gen_images_seed <out-dir> <seed> [count] [size]` — deterministic
  low-color alternate-input generator for the generalization check.
- `Makefile.optimized` — the optimized build. **Extend it only** if your code
  adds a new source file; never touch a reference target.

Do not modify `src/`, `test/`, `performance-test/`, or the committed `images/`.

## Goal

A platform-specific optimized version of the PEP compressor that is **as fast as
possible** at compression on this machine while producing output that, **once
decompressed, is pixel-identical to the reference**, whose `.pep` is **never
larger** than the reference's, and whose decompression is **net-neutral-or-better**.

Target framing (this was reset after measurement; do not treat "10x" as the
goal): the achievable speedup is a **frontier set by the size contract**, not a
single number — measured on the committed 24-image benchmark:
- **strict "never larger" (the locked baseline): ~2x** (per-image ~1.5–2.6x);
- ~5x is reachable if you allow `.pep` up to ~1.13x of reference (sample-based
  model selection);
- ~9x only if you abandon the size guarantee (single-model shortcut, +up to 35%).
The earlier "10x / 6–18x" was the size-breaking regime. Aim to push the
**strict** number up without violating the size or decode gates.

## The match contract — pixel-identity after decompression (relaxed)

PEP must stay **lossless**: for every image, `decompress(compress(x))` must
reproduce the quantized input `x` exactly (no pixel ever changes). What is **no
longer required** is that the compressed `.pep` bytes match the reference's. The
optimized compressor MAY use a **different file format, a different entropy
coder, a different model set, or a different number of trials** — anything —
**so long as its own decompressor reads its own output and yields pixels
identical to the reference's decompressed pixels** (which equal the quantized
input). Verified for every input set with `build/compare_results`, which compares
the **decompressed pixels** (not the `.pep` bytes). An optimization that changes
a single output **pixel** is rejected, however fast; an optimization that changes
the compressed **bytes** but decompresses to the same pixels is **allowed**.

Why this was relaxed: under the previous byte-identity contract the only legal
changes were ones that computed the *same* encoding faster, and the encode/
arithmetic-coder path is ~97% of runtime and inherently sequential — so 10x was
shown to be structurally unreachable (best proven was ~2.1x). Allowing the output
*representation* to change (a faster entropy coder, fewer model trials, a leaner
format) opens the headroom needed to chase 10x.

**You may now edit the decompressor too.** Because the format can change, the
optimized `pep_decompress` must stay consistent with the optimized
`pep_compress` (the optimized harness decompresses optimized `.pep` with the
optimized library). Keep both in `src-optimized/` and keep them in lock-step:
the lossless round-trip gate and the decompressed-pixel comparator both fail if
they drift apart. Do **not** touch the reference `src/` or the reference
decompressor.

Hard limits that remain: still **lossless** (pixels exact — quantization already
happened upstream and is not part of this; you may not introduce any further
pixel loss), still **deterministic** (same input bytes → same output bytes every
run), still **single-threaded**, and the format must be **self-contained** (the
`.pep` carries everything its own decompressor needs).

**The size floor (closes the obvious hole).** The optimized `.pep` may be a
different size than the reference's, but it must **never be larger** — for
**every** image, `opt.pep_bytes <= ref.pep_bytes`. Otherwise "faster" could be
bought by emitting a cheaper-but-bigger encoding (in the limit, near-raw bytes),
which decompresses to the same pixels but defeats the purpose of a compressor.
`build/compare_results` enforces this (`size_regressions` must be 0); an
optimization that makes any image's `.pep` larger is **rejected, however fast**.
Smaller-or-equal is fine and welcome; larger is a FAIL.

The size gate is a bound, **not** a reason to avoid changing the coder/format.
A different entropy coder is allowed and encouraged *if it matches or beats the
reference's ratio*. Naive formats (raw indices, plain RLE, generic deflate,
static Huffman) tend to come out **larger** than the reference's context model
and will fail — but that means "match the reference's modeling power with a
faster coder," not "never change the coder." De-risk it cheaply: estimate the
new scheme's size on a **sample** (or one image) *before* committing to the full
rewrite, so the size gate is something you've already checked, not something you
fear.

**The decompression floor (you optimize compression, but must not regress
decode).** The target is *compression* speed, but a format/coder change can make
**decompression** slower as a side effect — and a faster compressor that ships a
slower decoder is a bad trade. So the optimized decode is gated too: for the
committed set, the optimized `pep_decompress` median must be **net-neutral or
better** versus the reference decode — `opt_decode <= ref_decode` within a small
noise band (`DECOMP_TOL`, default 3%). `prove-optimized-harness.sh` measures this
(`build/decomp_time` on reference `.pep`, `build/decomp_time_opt` on optimized
`.pep`, median-of-`DECOMP_RUNS`) and **FAILS the proof on a decode regression**,
even if compression hit the target. Faster decode is welcome; slower decode is a
FAIL.

**A decode regression does not disqualify a compression change — it creates a
second task.** When a compression change (that is otherwise correct:
pixel-identical, lossless, `.pep` not larger) reds *only* the decode gate, do
**not** reflexively revert it. The decode gate failing means you now have a
follow-on optimization to do — *speed up `pep_decompress`* — and the right move
is to **attempt that decode optimization and re-measure before deciding**. Treat
"optimize the decoder" as its own first-class candidate (profile/sample the
decode path the same way you did the encode path; the slowdown usually has a
specific, fixable cause — a reordered struct, a lost fast path, an extra pass).
Keep the compression change and the decode fix together as one **paired arc**
(see step 6's enabling-transform rule) and gate the *combined* state. Only
**drop the compression change if you have actually tried and cannot bring decode
back to parity** — and then say so with the measurement (e.g. "format X decodes
N% slower intrinsically; tried Y and Z, best was still +M% → reverted"). Do not
let a promising compression win die just because its first cut happened to slow
decode.

## Fixed constraints (inherited from the reference — do not relax any)

- Same functional behavior as the reference: ≤256-color palette, the four PEP
  formats/bit-depths supported, deterministic (same input bytes → same output
  bytes).
- **Single-threaded only.** No threads. SIMD within one thread is allowed and
  expected.
- C11, gcc, libc + libm + the vendored single-headers only. Build with
  `-Wall -Wextra -Werror` (vendored TUs may add `-Wno-unknown-pragmas`).
- **Platform-specific optimization is the point**: x86_64 intrinsics, SSE / AVX
  / FMA, `-march=native`, cache-conscious layout — anything **this host actually
  supports**. Inspect the host first (`/proc/cpuinfo`, `gcc -march=native -Q
  --help=target`, cache sizes) and record what you found; do not assume a
  feature exists.
- **Quantization (pre-processing) is excluded from the timed compression**, but
  it must remain a general transform over any image and must NOT precompute the
  compressed stream. The timed `pep_compress` must perform the actual
  entropy/PPM coding — moving the compression itself into the build stage and
  having the runtime memcpy a precomputed `.pep` is cheating and is forbidden.
  (The harness only ever calls `pep_compress` inside the timed region on the
  quantized pixels, so keep it that way.)
- **No overfitting — but exploiting the data *class* is encouraged, not banned.**
  The line: do not key to the *specific committed files* (no constants derived
  from their particular content, no table memorizing their exact streams). You
  **may and should** exploit *structural properties of the input class* that a
  data profile reveals and that hold for any valid input — e.g. "palette-index
  streams of quantized images are skewed / have runs," which justifies a
  most-probable-symbol or run fast path for *any* such image. That is "exploit
  every constraint," not overfitting. The Phase 4 generalization check (alternate
  synthetic inputs) is exactly what proves a pattern is class-wide rather than
  file-specific — so a data-pattern specialization that still passes
  generalization is legitimate by construction. Don't let "no overfitting" talk
  you out of (d).

## Phase 1 — Plan and baseline (before optimizing)

1. Write `src-optimized/README.md`: host CPU features and cache sizes as
   measured; where the reference spends its time — profile **where the cycles
   go** (`perf record`/`perf stat` if available, else coarse manual timers around
   the model-encode and model-selection loops in `pep_compress`; state which you
   used). A call-count profile is not enough: throughput/SIMD/layout wins remove
   no calls, they compress the cycles per call.
   **Also profile the *data*, not just the cycles.** The cycle profile says
   *where* time goes; a data profile says *what to exploit*. Temporarily
   instrument the hot path and sample the actual values flowing through it — the
   per-context symbol distributions (how skewed?), run-lengths of the index
   stream (how runny?), which model actually wins and how often, the real
   alphabet size per context — then histogram/sort/count and look for patterns
   (see the "Sample the data you already have" rule in the data-oriented-design
   context). Those patterns are exactly what feeds candidate kinds (c) and (d)
   below; without them the loop only ever produces (a)/(b) micro-tweaks. Remove
   the probes before measuring. Then a candidate list **ranked by expected
   payoff** = fraction of measured runtime a change touches × the speedup you
   expect on that fraction (Amdahl) — not by op-count or effort. Mark every
   unverifiable fact as `ASSUMPTION: <fact> — affects <decision>`.
2. The **measurement protocol** already exists (`measure-speedup.sh`, median of
   5). Use it for every number; do not invent a second protocol.
3. Measure the reference baseline with it and record it.

## Phase 2 — Implement the optimized compressor

`src-optimized/pep.h` already starts as a correct copy. Begin from it and
optimize. Never **commit or carry forward** a broken state: every kept iteration
ends with a clean build and a passing test suite. Attempting a hard change that
breaks is fine — revert it. Do not let "keep the tree clean" talk you out of a
high-payoff change.

## Phase 3 — Iterate (the core loop)

Repeat until an exit criterion is met. Each iteration:

1. **Pick** the highest-expected-payoff candidate (runtime-fraction × expected
   speedup), spanning **four kinds** of change — all first-class; do not treat
   (c) and (d) as detours:
   - **(a) work removal** — skip redundant passes, cheaper math, fewer model
     trials.
   - **(b) throughput / data layout** — SoA over palette indices, branch-free
     per-type inner loops, alignment, SIMD (SSE/AVX/FMA) over the prediction and
     model-selection loops that dominate runtime.
   - **(c) representation / algorithm change** — a *different entropy coder*
     (e.g. a range coder, rANS/tANS), a *different model*, or a *different
     `.pep` format*. The relaxed contract exists precisely to allow this: the
     bytes may change as long as decompression yields identical pixels and the
     file is not larger. The reference's adaptive arithmetic coder being ~97% of
     runtime and inherently sequential is an argument *for* trying a different
     machine, not for only filing the current one. When micro-tweaks of the
     existing coder stop paying, this is the category to reach for.
   - **(d) data-pattern specialization** — exploit a *measured* distribution
     from the data profile below: a skewed context → a most-probable-symbol fast
     path; long runs → run-aware coding; a near-constant value → hoist it. This
     is "the common case dominates" applied to the actual coded data.
   A throughput, representation, or data-pattern change removes no calls, so a
   call-count profile ranks it ≈0 — rank it by runtime-fraction × expected
   speedup on that fraction or you will never select it. Implementation risk is
   not a selection criterion: a failed attempt costs one iteration and a logged
   negative result. **When you have plateaued** — several consecutive reverts, or
   micro-tweaks stuck below target — stop filing the current machine: re-profile
   the *data* (below) and evaluate a (c) or (d) candidate.
2. **Implement** that one change in `src-optimized/pep.h`.
3. **Gate on correctness**, in order, using the existing harness; a failure at
   any gate means fix or revert before measuring. **Wrap every gate command that
   runs the optimized binary in `timeout` (e.g. `timeout 600 ./build/test_runner_opt`,
   `timeout 600 ./run-performance-test-optimized`)** — a buggy optimization can
   infinite-loop, and an un-timed gate will hang the whole loop indefinitely
   instead of failing fast. A timeout (exit 124) is a FAIL: revert the change.
   `prove-optimized-harness.sh` and `measure-speedup.sh` already apply generous
   timeouts internally; mirror that for any gate you run by hand:
   - clean build (`make -f Makefile.optimized optimized`), full suite passes
     (`build/test_runner_opt`);
   - `./run-performance-test-optimized`, then `build/compare_results
     performance-test/results.txt performance-test-optimized/results.txt`
     reports **PASS** (decompressed pixels identical, 0 round-trip failures, 0
     size regressions — `.pep` format/bytes may differ and may shrink, but no
     image's `.pep` may be larger than the reference's), committed images checksum
     unchanged;
   - two consecutive optimized runs produce byte-identical results (determinism
     is still byte-level: the same code must emit the same bytes every run);
   - **decompression is net-neutral or better**: `build/decomp_time_opt` on the
     optimized `.pep` is `<=` `build/decomp_time` on the reference `.pep` (within
     `DECOMP_TOL`, default 3%), median-of-5. `prove-optimized-harness.sh` checks
     this each run. A decode regression is a FAIL **of this gate**, not a verdict
     on the compression change: if the change is otherwise correct, **first try
     to optimize the decoder back to parity** (a paired arc, kept together), and
     revert the compression change *only if* decode genuinely can't be recovered
     — with the measurement that shows it.
4. **Measure** with the protocol (median of 5).
5. **Record** in `src-optimized/OPTIMIZATION-LOG.md`: hypothesis, change,
   before/after medians, speedup vs reference so far, keep/revert decision, and
   the cost of the hypothesis (wall-clock + tokens).
6. **Keep or revert — and make every kept state durable.** Keep a change if it
   passed every gate and either measurably helps, is a pure simplification, or is
   a correctness-preserving net-neutral **enabling transform** (branch-free /
   SoA / alignment that unlocks a named next optimization). When you keep, commit
   immediately; otherwise revert to the last committed baseline. A rejected
   hypothesis with its measurement is a result — keep the log entry.
   **Paired arc (compression change + its decode fix).** If a compression change
   helps and is otherwise correct but reds the decode gate, it is *not* an
   automatic revert: pair it with a decode optimization and judge the **combined**
   state — keep both together once the combined state passes *every* gate
   (including the decode gate). Develop and commit the arc as a unit; only after a
   genuine, measured attempt to recover decode fails do you revert the compression
   change. Don't discard a compression win before the decode half of the arc has
   been tried.
7. **Update** the candidate list.

Never stack multiple *independent* speculative changes into one measurement; you
must attribute each change's effect. A **representation/algorithm change (c)** is
the one coherent exception: a new entropy coder or format is one *logical* change
even though it touches encoder + decoder + format together — develop it as a unit
(prototype it in a copy if that helps), gate it as a unit, and attribute its net
effect against the last committed baseline. "One change at a time" means don't
also tweak the layout in the same measurement — it does not mean structural work
is forbidden.

## Phase 4 — Generalization check (mandatory before claiming any exit)

1. Generate ≥2 synthetic alternate sets with `build/gen_images_seed` (different
   seeds), never touching the committed `images/`.
2. For each: run both harnesses and `compare_results` — must be **pixel-identical
   PASS** — and report the measured speedup alongside the committed-input speedup.
3. If speedup collapses or output diverges on alternate data, the optimization
   is overfit or wrong: diagnose, fix or revert, return to Phase 3.

(`prove-optimized-harness.sh` runs all of these gates end to end.)

## Exit criteria (exactly one must be met, with evidence)

- **IMPROVED BASELINE**: median measured compression time on the committed images
  is **faster than the current locked strict baseline (~2x)** with **all** Phase 3
  gates green (pixel-identical, lossless, **`.pep` never larger**, **decode
  net-neutral-or-better**) and the Phase 4 generalization check passing. There is
  no fixed "10x" finish line — any honest, all-gates-green speed improvement over
  the strict baseline is a win; record the new number. A speed win that trips the
  size or decode gate is **not** this criterion.
- **NO FURTHER OPPORTUNITIES**: the candidate list is empty — every candidate was
  implemented and kept, implemented/measured/reverted with numbers, or rejected
  with a stated reason (e.g. "requires threads — prohibited", "changes a decoded
  pixel — breaks losslessness/pixel-identity", "introduces pixel loss"). This
  criterion may **not** be claimed while the list is still only (a)/(b)
  micro-tweaks: it requires that at least the highest-payoff **(c)
  representation/algorithm** and **(d) data-pattern** candidates from a real data
  profile were either tried-and-measured or rejected with a concrete,
  data-backed reason (e.g. "sampled ratio of scheme X exceeds the reference on
  image N → would fail the size gate"). "I optimized the existing coder as far as
  it goes" is not the same as "no further opportunities." Report the best
  achieved speedup. "I ran out of ideas" without a log of tried-and-measured
  candidates is not this criterion.

Do not stop for any other reason. Do not claim the target from a projection or a
single noisy run.

## Final report (whichever exit is taken)

- Measured reference baseline and final optimized time (protocol medians) and the
  speedup factor — on this machine, no projections.
- The optimization log: what was tried, what each change measured, what was kept.
- Generalization results on the alternate sets.
- Verification evidence: build output, test output, the decompressed-pixel
  comparison, checksum checks, determinism check — actual commands and output.
- Anything not verified, stated explicitly, with why.

## Acceptance checklist

- [ ] `src-optimized/README.md`: host inspection, reference profile, ranked
      candidate list, labeled ASSUMPTIONs.
- [ ] `measure-speedup.sh` used for every number; no second protocol invented.
- [ ] Reference baseline measured on this machine.
- [ ] Builds clean; libc/libm + vendored single-headers only; single-threaded.
- [ ] Full reference suite passes against the optimized library.
- [ ] Optimized output **pixel-identical after decompression** to the reference
      via `compare_results` on the committed images **and** ≥2 alternate sets;
      lossless round-trip on both sides; **no `.pep` larger than the reference's**
      (0 size regressions). `.pep` format may differ and may shrink, never grow.
- [ ] **Decompression net-neutral or better**: `build/decomp_time_opt` <=
      `build/decomp_time` (within `DECOMP_TOL`) on the committed set; no decode
      regression introduced by the optimization.
- [ ] Committed `images/` never modified (checksum evidence); alternate sets
      generated with `gen_images_seed`.
- [ ] Quantization excluded from reported time; the timed step performs the real
      compression (no precomputed `.pep` smuggled through the build stage).
- [ ] `OPTIMIZATION-LOG.md` has one entry per iteration with before/after
      measurements, keep/revert decisions, and per-hypothesis cost.
- [ ] Only `src-optimized/` (and required build wiring) changed; no harness piece
      weakened or forked.
- [ ] Exit criterion explicitly identified as TARGET REACHED or NO FURTHER
      OPPORTUNITIES, with the evidence that criterion demands.
- [ ] No unmeasured performance claim anywhere.

# RSX 1.25x focused validation: retain opt-in status

Production source is unchanged from PR #3 commit c2d706f71ff22a1415331f033a2c2e291226b6d9.
The first focused run is https://github.com/kohakunamori/rpcs3/actions/runs/40204026296
at source commit 8ae4a69f47bde266648fafc876d4fcd3f0561d2d.

## Verified

- Both actual container policies compiled with MSVC Release and AddressSanitizer.
- ASan boundary/content/alignment checks passed for both (26 type/size combinations
  per policy, including zero, local-buffer/growth transitions and 64-byte alignment).
- 78 workload cases completed with 12 balanced AB/BA pairs; every raw sample
  passed checksum, repeat-count, policy and schema validation.
- The Python validator's 12 tests passed on Linux locally and the Windows runner.
- Raw artifact id 12602659198, name rsx-optin-focused-validation, includes binary
  hashes, compiler settings, CPU, ASan output and full CSVs. Host: Windows Server
  2025, MSVC 14.51.36231, AMD EPYC 9V45 virtual runner, 4 logical processors.

## Preliminary measurements (batch totals, not per-operation times)

The change column is the median of 12 paired candidate/baseline ratios, so it
need not equal the ratio of the two independently reported median durations.

| Type, size, mode | Baseline median ms | Candidate median ms | Paired change | Faster pairs |
| --- | ---: | ---: | ---: | ---: |
| record64, 8192, cold | 46.60815 | 7.57300 | -83.72% | 12/12 |
| u64, 8192, cold | 5.31340 | 0.31040 | -94.24% | 12/12 |
| record64, 25, reserved | 0.30570 | 0.35515 | +16.25% | 0/12 |
| record64, 25, reuse | 0.31100 | 0.35280 | +13.48% | 0/12 |
| record64, 8192, reuse | 0.34505 | 0.36580 | +6.53% | 0/12 |
| record64, 8, cold | 1.26705 | 1.29955 | +0.73% | 5/12 |

Only record64/8192/cold had both median batch durations at least 5ms. **77 of
78 cases were flagged short_batch**, so small differences and regression signals
require longer measurements. Cold large append gains cannot establish a benefit
for retained capacity, where no reallocations occur during the timed loop.
Source-code layout/inlining differences may still affect this loop; the exact
cause has not been established. Do not discard the slower outcomes.

## Follow-up implemented

`codex/rsx-array-long-batches` adds an explicit checked batch multiplier and
repeats all workloads at 128x work per sample, still 12 balanced pairs, with
identical repeat counts in both policies. Assembly listings are retained for
later code-generation inspection. Three additional runner tests cover scale
validation. This is a separate lightweight workflow, not another full-emulator
build. The full opt-in test build on the validation branch is left undisturbed.

Do not merge or enable by default based only on append gains. The actual game's
allocation/reuse distribution, peak-memory effects and full opt-in integration
results remain prerequisites. No game FPS measurements have been performed.

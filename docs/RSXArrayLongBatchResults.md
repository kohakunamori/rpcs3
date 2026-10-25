# RSX 1.25x: long-batch evidence and code-generation isolation

## Completed measurements

Source: `24a04e3d948e7b1da66a89f72f725efd24d22eb6`.
Run: https://github.com/kohakunamori/rpcs3/actions/runs/40204399423
Artifact: `12603840496`, `rsx-long-batch-results`, ZIP SHA256
`e1b90493134295756f7ccc339d8d95b152799b9a7df568fc186368c88a9c247f`.
Windows Server 2025 / MSVC 19.51.36260, 4-logical-processor virtual runner.
All 78 cases completed 12 balanced AB/BA pairs at batch scale 128. No case
has a median batch below 5 ms. This is still one machine/run, not game FPS.

The percentage is the median of paired candidate/baseline ratios, not the
ratio of the separately reported medians. Durations are whole batches.

| Case | Baseline ms | Candidate ms | Paired change | Faster pairs |
| --- | ---: | ---: | ---: | ---: |
| record64 / 8192 / cold | 5264.09485 | 374.89110 | -92.8159% | 12/12 |
| u64 / 8192 / cold | 993.98620 | 49.10900 | -94.8914% | 12/12 |
| record64 / 25 / reserved | 52.95295 | 53.24855 | +0.1408% | 3/12 |
| record64 / 25 / reuse | 52.97320 | 52.95820 | -0.0801% | 7/12 |
| u64 / 9 / reserved | 55.25390 | 72.92670 | +34.2546% | 0/12 |
| u64 / 9 / reuse | 54.05590 | 68.18390 | +25.0498% | 0/12 |
| u64 / 25 / reserved | 35.74295 | 37.90725 | +6.5830% | 0/12 |
| u64 / 25 / reuse | 36.49955 | 40.03580 | +8.5542% | 1/12 |

The earlier record64 short-batch regression did not reproduce at this scale;
that does not erase the small-u64 regressions. All retained/reserved timed
loops report zero growths. In particular, n=9 has identical capacity in both
policies, so extra reserved capacity is not an explanation of that case.
The exact cause is not yet established; code generation/layout is a hypothesis.

## Evidence-collection defect

The old workflow used `/Fa:baseline.asm` and `/Fa:candidate.asm`. The downloaded
artifact contains neither promised listing. The documented MSVC syntax is
`/Fa` immediately followed by the pathname (no colon separator). The upload
step's `if-no-files-found: error` did not reject a partially missing file set.
Do not claim that the original run supplied assembly evidence.

The new codegen workflow uses `/Facodegen/<variant>.asm`, retains object files,
binaries, compiler output, flags and hashes, and fails if any required listing
or other core artifact is absent or empty. Seven guard tests cover this logic.
Reference: https://learn.microsoft.com/en-us/cpp/build/reference/fa-fa-listing-file

## Follow-up experiment, not a default change

Branch `codex/rsx-array-codegen-isolation` applies a narrow, recorded patch only
to the CI checkout: conditionally prevent `grow_for_append` from inlining.
Three independently paired comparisons are run with 12 pairs and scale 128:

1. Original +16 versus original 1.25x (replication with complete evidence).
2. Both policies with growth outlined (isolate policy arithmetic).
3. Original +16 versus outlined 1.25x (the actual candidate decision).

Each comparison builds release and ASan variants, runs correctness checks,
and preserves the source patch and before/after hashes. Do not compare raw
absolute times between matrix jobs: they can use different virtual hosts.
No claim that outlining fixes the regression is justified until results exist.

PR #3 stays Draft and opt-in. Master and the PR's production source are not
changed by this branch. Full opt-in integration, capacity-consumer audit,
real-game allocation/reuse distribution, peak RAM and frame times remain gates.

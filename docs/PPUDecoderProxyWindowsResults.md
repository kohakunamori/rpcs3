# PPU decoder fast-path structural proxy: Windows MSVC results

This report records **synthetic structural-proxy** measurements, not actual
RPCS3 decoder throughput and not in-game FPS. A standalone MSVC /O2 executable
uses synthetic handler and instruction-type arrays to compare the incremental
lookup-filter cost. The production `needs_special_decode` predicate is
extracted directly from `rpcs3/Emu/Cell/PPUInterpreter.h` by
`scripts/extract_ppu_fastpath_mask.py`.

The CPU model and process environment are not controlled beyond using a hosted
Windows runner, so these measurements should not be treated as statistically
significant evidence about emulator performance.

## First run (mask only)

Source: https://github.com/kohakunamori/rpcs3/actions/runs/40200552626

| Synthetic profile | Reference median | Mask median | Mask change |
| --- | ---: | ---: | ---: |
| 16 common instruction encodings | 15.002 ms | 30.585 ms | +103.872% |
| Uniform random opcodes | 18.211 ms | 26.894 ms | +47.683% |

## Follow-up (three filtering strategies)

Source: https://github.com/kohakunamori/rpcs3/actions/runs/40200682826

| Synthetic profile | Reference | Mask | Branch | Switch |
| --- | ---: | ---: | ---: | ---: |
| 16 common encodings | 16.870 ms | 31.877 ms | 31.849 ms | 37.877 ms |
| Uniform random | 20.671 ms | 28.464 ms | 45.805 ms | 38.796 ms |

Changes relative to the reference, in the same order:

- Common: mask +88.953%, branch +88.782%, switch +124.513%.
- Uniform: mask +37.699%, branch +121.592%, switch +87.684%.

The workloads are generated from fixed random seeds. Each path is warmed up,
measured in nine order-rotated pairs, and checked for the same synthetic
checksum. The full raw results are available as downloadable CSV artifacts
on each workflow run.

## Decision

All proposed extra-filter strategies were slower in this structural proxy.
Therefore **do not merge PR #2 based on a presumed lookup speedup**.
Its full RPCS3 unit-test differential check and paired benchmark are the
required evidence for deciding whether to reject or revise the candidate.
A real-game FPS benefit has not been established, and the proxy's slowdowns
cannot be interpreted as percentages of in-game performance loss.

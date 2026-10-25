# RSX simple_array experimental append-growth benchmark

This experiment compiles the **actual** `rsx::simple_array` header using MSVC `/O2` four times:

- `baseline`: upstream's existing `capacity + 16` append growth (also the unchanged default build)
- `grow50`: roughly 1.5x growth with a minimum of 16 extra slots
- `grow25`: roughly 1.25x growth with a minimum of 16 extra slots
- `grow125`: roughly 1.125x growth with a minimum of 16 extra slots

All builds run the same deterministic append work, verify every element, check that all variants produce the same checksum, and report actual capacity and observed capacity growth events.

## Initial result (1.5x only)

Windows CI: https://github.com/kohakunamori/rpcs3/actions/runs/40202479319

| Scenario | Baseline median (ms) | 1.5x median (ms) | Change | Baseline capacity | 1.5x capacity |
| --- | ---: | ---: | ---: | ---: | ---: |
| u64 8192, push_lvalue | 0.24 | 0.09 | -61.67% | 8200 | 11623 |
| u64 65536, push_lvalue | 10.01 | 0.42 | -95.83% | 65544 | 88258 |
| payload64 8192, push_lvalue | 1.48 | 0.40 | -72.80% | 8193 | 9369 |
| payload64 32768, push_lvalue | 114.52 | 1.71 | -98.51% | 32769 | 47427 |

These are microbenchmark timings on a Windows GitHub Actions runner, not emulator frame times. The geometric policy reduces repeated reallocations at the cost of retained unused capacity, particularly in large arrays. Runtime emulation logic and the default allocator policy remain unchanged until a defensible policy has been validated in a full RPCS3 build and game workload.

The second run compares 1.5x, 1.25x, and 1.125x to quantify this memory/throughput tradeoff. Report both latency and spare capacity; do not claim FPS improvements from this test.

## Second benchmark: four policies

Windows run: https://github.com/kohakunamori/rpcs3/actions/runs/40202638779

| Workload and operation | Baseline | 1.5x | 1.25x | 1.125x |
| --- | ---: | ---: | ---: | ---: |
| `payload64`, 32768, push_lvalue (ms) | 147.89 | 1.95 | 2.03 | 3.83 |
| `payload64`, 32768, spare capacity (%) | ~0 | 44.7 | 0.8 | 9.0 |
| `payload64`, 32768, grow events | 2048 | 20 | 32 | 56 |
| `u64`, 65536, push_lvalue (ms) | 14.14 | 0.47 | 0.45 | 0.45 |
| `u64`, 65536, spare capacity (%) | ~0 | 34.7 | 9.4 | 4.8 |

One hosted runner's microbenchmark timings are noisy, especially at sub-millisecond durations. The 1.25x scheme is a promising compromise in these tests, but it is not a confirmed best policy without multiple machines and full RPCS3 compatibility/regression evidence. Memory figures are **capacity slack**, not OS committed-memory measurements.

## Correctness follow-up

The standalone benchmark now runs deterministic randomized mixed `push_back`, `emplace_back`, `insert`, `pop_back`, `clear`, `reserve`, and copy-constructor checks against `std::vector`, for both baseline and candidate builds. The public `reserve(n)` contract is left untouched. A failure aborts the CI benchmark before any speed numbers are accepted.

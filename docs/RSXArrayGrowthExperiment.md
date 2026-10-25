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

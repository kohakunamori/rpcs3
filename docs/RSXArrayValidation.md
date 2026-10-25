# RSX growth: opt-in validation, not an FPS claim

The prior PR-wide build does not define the geometric-growth macro. Passing it
validates the default +16 path, **not** the experimental 1.25x path. This branch
keeps the production header unchanged from c2d706f71ff22a1415331f033a2c2e291226b6d9
and adds independent validation without restarting that in-progress PR build.

## Focused Windows checks

`scripts/rsx_array_validation.cpp` includes the actual RPCS3 header. Both the
baseline and opt-in policy are compiled with MSVC Release and AddressSanitizer.
ASan executions are correctness checks only, excluded from performance reports.
The standalone fatal-diagnostic stub is not a substitute allocator/container.

Each policy verifies 13 sizes for an 8-byte integer and a 64-byte, 64-aligned
record: empty, local-buffer transitions, multiple growth boundaries and 8192
entries. Tests cover both insert overloads, independent range insertion,
copy/move construction, mixed local/heap swap, copy assignment, exact explicit
reserve, retained-storage clear/refill, pop order, data and alignment. The
observed capacity schedule must match the requested compiled policy. Inputs do
not alias the destination and tests do not assert unsupported aliasing behavior.
This is finite coverage, not proof against every allocation failure or OOM case.

## Workload and reporting changes

The performance test covers 78 cases: 13 sizes x 2 element types x 3 modes.
`cold` includes construction, appending, a non-inlined checksum consumer and
destruction. `reserved` reserves before measurement. `reuse` fills once before
measurement, then repeatedly clears and refills retained storage. The latter
two exclude allocation setup/destruction from the timer. Do not compare their
absolute times against the cold mode as if they were the same workload.

Capacity/growth counters are collected outside the timed loop. All variants
perform exactly the same iteration count and produce a mathematical expected
checksum. Correctness executes in a separate process so it does not prime the
heap before the benchmark. Each complete benchmark process still has a fixed
case order; these are synthetic workloads, not recorded game traces.

The Python runner uses 12 balanced AB/BA pairs. It saves raw CSV, per-process
stdout/stderr, source commit/run, OS, CPU, compiler flags and binary SHA-256.
Missing/duplicate cases, invalid timings, bad checksums and mismatched work fail
validation. Short batches (<5 ms) and mixed pair directions are explicitly
flagged; timing never automatically passes or fails an optimization. Capacity
bytes are reserved payload bytes, **not** measured process RSS or committed RAM.
The result-validator has 12 local Python unit tests.

## Full opt-in integration

After focused checks pass, a separate Windows job compiles the emulator
libraries and full unit-test executable. `_CL_` appends the opt-in define to
all MSVC invocations, preventing translation units from mixing allocation
policies. Compiler-object caches are disabled for this job; dependency archives
may be restored. Full test XML, output and the MSBuild binary log are retained.
A passing standalone test does not count as a passed full integration job.

No default policy is changed here. Actual game traces, peak-memory measurements,
frame times, pointer-lifetime consumers and broader hardware remain outstanding.
A large append speedup is not a game FPS prediction. Neither 1.25x nor a given
spare-capacity percentage is universally optimal across sizes.

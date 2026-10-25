# Windows performance comparison

`scripts/benchmark_compare.ps1` runs baseline and candidate executables with the same argument list, performs warmups, alternates the order of measured pairs, and emits CSV and JSON reports plus environment/binary metadata. It does not change RPCS3 runtime behavior.

## Example: startup benchmark

Run from PowerShell in the repository root. Use matching builds and identical arguments:

```powershell
.\scripts\benchmark_compare.ps1 `
  -BaselineExecutable "C:\bench\baseline\rpcs3.exe" `
  -CandidateExecutable "C:\bench\candidate\rpcs3.exe" `
  -ArgumentList @("--version") `
  -WarmupRuns 2 `
  -MeasuredRuns 9 `
  -OutputDirectory ".\benchmark-results" `
  -WorkloadName "cli-startup"
```

This measures process startup/exit overhead and basic application initialization. It does **not** measure emulation throughput or in-game FPS.

## Measuring a repeatable emulation workload

For a meaningful emulator comparison, use a deterministic game/save-state workload or a repeatable in-game scene. Keep game version, firmware, configuration, renderer, shader-cache state, resolution, power profile, and test duration identical. Collect frame-time/FPS telemetry separately. Run each binary from isolated config/cache directories if a workload writes persistent state.

The current harness launches a process and waits for it to exit. It does not inject input or stop a game at a specific frame; it is suitable for finite CLI workloads and test executables, not an unattended full-game benchmark without an external workload controller.

## Output files

- CSV: raw measured samples, one row per variant per pair.
- JSON: raw samples in JSON form.
- Metadata JSON: command-line arguments, PowerShell/OS details, processor environment, executable SHA-256 hashes, file sizes, and version strings.

The summary uses median elapsed time and reports candidate change relative to baseline. Negative percentages mean faster. Pair order alternates baseline/candidate to reduce bias from drift, but this does not replace repeated full sessions or a statistical analysis.

## Important limitations

- Process lifetime, CPU time, and peak working set are not in-game FPS.
- The Windows workflow's harness-smoke uses the same no-op command for both variants to validate script operation only. Its timings are not a performance result.
- The script stops if either measured executable exits with a non-zero code so failed runs are not silently treated as successful measurements.
- Run a correctness/regression suite separately. Performance numbers alone do not prove semantic equivalence.
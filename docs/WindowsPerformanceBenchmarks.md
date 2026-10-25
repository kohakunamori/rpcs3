# Windows performance comparison

`scripts/benchmark_compare.ps1` runs a baseline executable and a candidate executable with the same argument list, records multiple process-level measurements, and emits CSV and JSON reports. It does not change RPCS3 runtime behavior.

## Example

Run from PowerShell in the repository root:

```powershell
.\scripts\benchmark_compare.ps1 `
  -BaselineExecutable "C:\bench\baseline\rpcs3.exe" `
  -CandidateExecutable "C:\bench\candidate\rpcs3.exe" `
  -ArgumentList @("--help") `
  -WarmupRuns 1 `
  -MeasuredRuns 7 `
  -OutputDirectory ".\benchmark-results" `
  -WorkloadName "startup"
```

Use the same RPCS3 command line, game, firmware, configuration, renderer, shader-cache state, and machine power profile for both variants. Keep the machine otherwise idle and repeat the whole comparison if the results are noisy.

## Important limitations

- This measures process lifetime, CPU time, and peak working set. It is **not** an in-game FPS benchmark and cannot establish an FPS improvement by itself.
- For meaningful emulation benchmarks, use a deterministic game/save-state workload or a repeatable in-game scene and collect frame-time/FPS telemetry separately. Do not compare different shader-cache warmup states.
- The script stops if either executable exits with a non-zero code so failed runs are not silently treated as successful measurements.
- A lower median elapsed time is faster; the reported percentage is candidate versus baseline. Negative values indicate improvement.
- Run a correctness/regression suite separately. Performance numbers alone do not prove semantic equivalence.

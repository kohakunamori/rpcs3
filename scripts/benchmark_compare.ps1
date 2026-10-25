param(
    [Parameter(Mandatory = $true)]
    [string]$BaselineExecutable,

    [Parameter(Mandatory = $true)]
    [string]$CandidateExecutable,

    [string[]]$ArgumentList = @(),

    [int]$WarmupRuns = 1,

    [int]$MeasuredRuns = 5,

    [string]$OutputDirectory = ".\benchmark-results",

    [string]$WorkloadName = "custom",

    [switch]$SuppressComparisonSummary
)

$ErrorActionPreference = "Stop"

if ($WarmupRuns -lt 0) { throw "WarmupRuns must be >= 0." }
if ($MeasuredRuns -lt 1) { throw "MeasuredRuns must be >= 1." }

function ConvertTo-WindowsCommandLineArgument {
    param([AllowEmptyString()][string]$Value)

    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    # Quote according to the Windows C runtime command-line parsing rules.
    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            $backslashes++
            continue
        }

        if ($character -eq '"') {
            [void]$builder.Append(('\' * (2 * $backslashes + 1)))
            [void]$builder.Append('"')
            $backslashes = 0
            continue
        }

        if ($backslashes -gt 0) {
            [void]$builder.Append(('\' * $backslashes))
            $backslashes = 0
        }
        [void]$builder.Append($character)
    }

    if ($backslashes -gt 0) {
        [void]$builder.Append(('\' * (2 * $backslashes)))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Get-ExecutableMetadata {
    param([string]$Executable)

    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw "Executable not found: $Executable"
    }

    $resolved = (Resolve-Path -LiteralPath $Executable).Path
    $file = Get-Item -LiteralPath $resolved
    $hash = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash

    return [pscustomobject]@{
        path = $resolved
        length_bytes = $file.Length
        sha256 = $hash
        file_version = $file.VersionInfo.FileVersion
        product_version = $file.VersionInfo.ProductVersion
    }
}

function Invoke-TimedRun {
    param(
        [string]$Label,
        [string]$Executable,
        [string[]]$Arguments,
        [int]$RunNumber,
        [int]$PairNumber,
        [bool]$IsWarmup
    )

    $resolved = (Resolve-Path -LiteralPath $Executable).Path
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $resolved
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.Arguments = (($Arguments | ForEach-Object {
        ConvertTo-WindowsCommandLineArgument -Value $_
    }) -join ' ')

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    if (-not $process.Start()) {
        throw "Failed to start $Executable"
    }
    $process.WaitForExit()
    $timer.Stop()

    $row = [pscustomobject]@{
        workload = $WorkloadName
        variant = $Label
        pair = $PairNumber
        run = $RunNumber
        warmup = $IsWarmup
        executable = $resolved
        elapsed_ms = [math]::Round($timer.Elapsed.TotalMilliseconds, 3)
        exit_code = $process.ExitCode
        cpu_time_ms = [math]::Round($process.TotalProcessorTime.TotalMilliseconds, 3)
        peak_working_set_bytes = $process.PeakWorkingSet64
    }
    $process.Dispose()

    if ($row.exit_code -ne 0) {
        throw "$Label run $RunNumber exited with code $($row.exit_code)."
    }
    return $row
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$results = [System.Collections.Generic.List[object]]::new()
$variants = @(
    [pscustomobject]@{ Name = "baseline"; Executable = $BaselineExecutable },
    [pscustomobject]@{ Name = "candidate"; Executable = $CandidateExecutable }
)

# Resolve and hash binaries before measuring so reports identify the exact inputs.
$binaryMetadata = @{}
foreach ($variant in $variants) {
    $binaryMetadata[$variant.Name] = Get-ExecutableMetadata -Executable $variant.Executable
}

# Warm both binaries before measured pairs.
foreach ($variant in $variants) {
    for ($i = 1; $i -le $WarmupRuns; $i++) {
        $null = Invoke-TimedRun -Label $variant.Name -Executable $variant.Executable -Arguments $ArgumentList -RunNumber $i -PairNumber 0 -IsWarmup $true
    }
}

# Alternate order each pair to reduce systematic bias from thermal/power drift.
for ($pair = 1; $pair -le $MeasuredRuns; $pair++) {
    if ($pair % 2 -eq 1) {
        $order = @($variants[0], $variants[1])
    } else {
        $order = @($variants[1], $variants[0])
    }

    foreach ($variant in $order) {
        $results.Add((Invoke-TimedRun -Label $variant.Name -Executable $variant.Executable -Arguments $ArgumentList -RunNumber $pair -PairNumber $pair -IsWarmup $false))
    }
}

$csvPath = Join-Path $OutputDirectory "$WorkloadName-$timestamp.csv"
$jsonPath = Join-Path $OutputDirectory "$WorkloadName-$timestamp.json"
$metadataPath = Join-Path $OutputDirectory "$WorkloadName-$timestamp-metadata.json"
$results | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding utf8

$metadata = [pscustomobject]@{
    timestamp_utc = [DateTime]::UtcNow.ToString("o")
    workload = $WorkloadName
    arguments = $ArgumentList
    warmup_runs_per_variant = $WarmupRuns
    measured_pairs = $MeasuredRuns
    pair_order = "Alternating baseline/candidate order by pair number"
    powershell_version = $PSVersionTable.PSVersion.ToString()
    os_version = [Environment]::OSVersion.VersionString
    processor_identifier = $env:PROCESSOR_IDENTIFIER
    logical_processor_count = [Environment]::ProcessorCount
    baseline = $binaryMetadata["baseline"]
    candidate = $binaryMetadata["candidate"]
    notes = "Process-level measurements only. This is not an in-game FPS benchmark."
}
$metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $metadataPath -Encoding utf8
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $jsonPath -Encoding utf8

if (-not $SuppressComparisonSummary) {
    $summary = foreach ($variantName in @("baseline", "candidate")) {
        $samples = @($results | Where-Object { $_.variant -eq $variantName } | ForEach-Object { [double]$_.elapsed_ms } | Sort-Object)
        $median = if ($samples.Count % 2) {
            $samples[[int][math]::Floor($samples.Count / 2)]
        } else {
            ($samples[$samples.Count / 2 - 1] + $samples[$samples.Count / 2]) / 2
        }
        [pscustomobject]@{
            variant = $variantName
            runs = $samples.Count
            median_elapsed_ms = [math]::Round($median, 3)
            min_elapsed_ms = $samples[0]
            max_elapsed_ms = $samples[-1]
        }
    }

    $baselineMedian = [double]($summary | Where-Object variant -eq "baseline").median_elapsed_ms
    $candidateMedian = [double]($summary | Where-Object variant -eq "candidate").median_elapsed_ms
    $changePercent = if ($baselineMedian -gt 0) {
        [math]::Round((($candidateMedian - $baselineMedian) / $baselineMedian) * 100, 2)
    } else { $null }

    Write-Host ""
    $summary | Format-Table -AutoSize
    Write-Host "Candidate median change: $changePercent% (negative is faster)"
}

Write-Host "CSV:      $csvPath"
Write-Host "JSON:     $jsonPath"
Write-Host "Metadata: $metadataPath"

param(
    [Parameter(Mandatory = $true)]
    [string]$BaselineExecutable,

    [Parameter(Mandatory = $true)]
    [string]$CandidateExecutable,

    [string[]]$ArgumentList = @(),

    [int]$WarmupRuns = 1,

    [int]$MeasuredRuns = 5,

    [string]$OutputDirectory = ".\benchmark-results",

    [string]$WorkloadName = "custom"
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

function Invoke-TimedRun {
    param(
        [string]$Label,
        [string]$Executable,
        [string[]]$Arguments,
        [int]$RunNumber,
        [bool]$IsWarmup
    )

    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        throw "Executable not found: $Executable"
    }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Resolve-Path -LiteralPath $Executable).Path
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
        run = $RunNumber
        warmup = $IsWarmup
        executable = $startInfo.FileName
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

foreach ($variant in @(
    @{ Name = "baseline"; Executable = $BaselineExecutable },
    @{ Name = "candidate"; Executable = $CandidateExecutable }
)) {
    for ($i = 1; $i -le $WarmupRuns; $i++) {
        $null = Invoke-TimedRun -Label $variant.Name -Executable $variant.Executable -Arguments $ArgumentList -RunNumber $i -IsWarmup $true
    }
    for ($i = 1; $i -le $MeasuredRuns; $i++) {
        $results.Add((Invoke-TimedRun -Label $variant.Name -Executable $variant.Executable -Arguments $ArgumentList -RunNumber $i -IsWarmup $false))
    }
}

$csvPath = Join-Path $OutputDirectory "$WorkloadName-$timestamp.csv"
$jsonPath = Join-Path $OutputDirectory "$WorkloadName-$timestamp.json"
$results | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding utf8
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $jsonPath -Encoding utf8

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
Write-Host "CSV:  $csvPath"
Write-Host "JSON: $jsonPath"

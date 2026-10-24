$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Native window probe regression is restricted to isolated CI' }
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe = Join-Path $project 'output/WeaselSkinPreview.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'Native preview is missing' }
$revision = (& git -C $project rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $revision -cnotmatch '^[0-9a-f]{40}$') { throw 'Source revision unavailable' }
function RunProbe([string[]]$Arguments, [int]$ExpectedExit) {
  $process = Start-Process -FilePath $exe -ArgumentList $Arguments -WorkingDirectory $project -WindowStyle Hidden -PassThru
  try {
    if (!$process.WaitForExit(120000)) { $process.Kill(); throw 'Native performance probe timed out' }
    if ($process.ExitCode -ne $ExpectedExit) {
      $log = Join-Path $project 'native-performance-probe-test.log'
      if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log }
      throw "Unexpected probe exit code: $($process.ExitCode), expected $ExpectedExit"
    }
    return $process.Id
  } finally { $process.Dispose() }
}
foreach ($duration in @('0', '121', '-1', '1x')) {
  RunProbe @('--performance-probe-test', 'true', '--probe-duration', $duration) 2 | Out-Null
}
RunProbe @('--probe-duration', '30') 2 | Out-Null
RunProbe @('--probe-duration') 2 | Out-Null
RunProbe @('--probe-revision') 2 | Out-Null
RunProbe @('--performance-probe-test', 'true', '--probe-duration') 2 | Out-Null
RunProbe @('--performance-probe-test', 'true', '--probe-revision', 'not-a-revision') 2 | Out-Null
$archive = Join-Path $project 'output/archives'
New-Item -ItemType Directory -Path $archive -Force | Out-Null
$timings = [Collections.Generic.List[object]]::new()
foreach ($sample in @(@('neutral', 'light'), @('shorekeeper', 'plush'))) {
  $skin = $sample[0]
  $manifest = Join-Path $project "skins/$skin/manifest.json"
  # Start-Process joins its argument array; quote the path explicitly for spaces.
  $arguments = @('--manifest', ('"' + $manifest + '"'), '--variant', $sample[1],
    '--performance-probe-test', 'true', '--probe-revision', $revision, '--probe-duration', '30')
  $probePid = RunProbe $arguments 0
  $path = Join-Path $project 'native-performance-probe-test.json'
  $result = Get-Content -LiteralPath $path -Raw -Encoding utf8 | ConvertFrom-Json
  if ($result.schemaVersion -ne 1 -or $result.finalRedrawEquivalent -ne $true -or @($result.runs).Count -ne 12) {
    throw 'Invalid ABBA report or mismatched output pixels'
  }
  $expectedModes = @('disabled', 'enabled', 'enabled', 'disabled')
  $measuredDpi = 0
  for ($index = 0; $index -lt 12; ++$index) {
    $run = $result.runs[$index]
    if ($run.group -ne [math]::Floor($index / 4) + 1 -or $run.phase -ne $index % 4 + 1 -or
        $run.mode -ne $expectedModes[$index % 4] -or $run.frames -ne 200 -or
        $null -eq $run.wallMs -or ![double]::IsFinite($run.wallMs) -or $run.wallMs -lt 0) {
      throw 'Invalid synthetic ABBA sequence or wall-clock timing'
    }
    if ($run.mode -eq 'disabled') {
      if ($null -ne $run.probe) { throw 'Disabled run fabricated a sampling report' }
      continue
    }
    $report = $run.probe
    if ($report.schemaVersion -ne 1 -or $report.sourceRevision -ne $revision -or
        $report.pid -ne $probePid -or $report.bitness -ne 64 -or $report.backend -ne 'gdi' -or
        $report.scenarioId -ne 'native.preview.hot' -or $report.requestedDurationSeconds -ne 30 -or
        $report.dpi -lt 1 -or $report.dpi -gt 768 -or $report.durationMs -lt 0 -or $report.durationMs -gt 30000) {
      throw 'Native probe metadata did not identify the real preview endpoint'
    }
    if (!$measuredDpi) { $measuredDpi = $report.dpi }
    if ($measuredDpi -ne $report.dpi) { throw 'A single probe scenario mixed monitor DPI values' }
    foreach ($name in @('core_process_key', 'context_response', 'ipc_roundtrip', 'present_complete')) {
      $stage = $report.stages.$name
      if ($stage.status -ne 'unavailable' -or $stage.samples -ne 0 -or $null -ne $stage.p50Ms -or
          $null -ne $stage.p95Ms -or $null -ne $stage.maxMs) { throw "Fabricated timing for $name" }
    }
    foreach ($name in @('layout', 'resource_prepare', 'paint_submit')) {
      $stage = $report.stages.$name
      if ($stage.status -ne 'measured' -or $stage.samples -lt 1 -or $stage.samples -gt 65536 -or
          $stage.dropped -lt 0 -or $null -eq $stage.p50Ms -or $null -eq $stage.p95Ms -or
          $null -eq $stage.maxMs -or ![double]::IsFinite($stage.maxMs) -or
          $stage.p50Ms -lt 0 -or $stage.p50Ms -gt $stage.p95Ms -or $stage.p95Ms -gt $stage.maxMs) {
        throw "Missing or invalid real timing for $name"
      }
    }
    if ($report.counters.frame_submit -lt 1) { throw 'No successful native submission was observed' }
  }
  foreach ($group in 1..3) {
    $rows = @($result.runs | Where-Object group -eq $group)
    $disabled = ($rows | Where-Object mode -eq 'disabled' | Measure-Object wallMs -Average).Average
    $enabled = ($rows | Where-Object mode -eq 'enabled' | Measure-Object wallMs -Average).Average
    $timings.Add([pscustomobject]@{ skin = $skin; group = $group; dpi = $measuredDpi;
      frames = 200; disabledMs = $disabled; enabledMs = $enabled;
      ratio = $(if ($disabled -gt 0) { $enabled / $disabled } else { $null }) })
  }
  Copy-Item -LiteralPath $path -Destination (Join-Path $archive "performance-preview-$skin.json")
  Copy-Item -LiteralPath (Join-Path $project 'native-performance-probe-test.log') -Destination (Join-Path $archive "performance-preview-$skin.log")
  Write-Output "PASS real native probe hooks: $skin, three ABBA groups, equivalent final redraws, DPI $measuredDpi"
}
$timings | Export-Csv -LiteralPath (Join-Path $archive 'performance-probe-overhead.csv') -NoTypeInformation -Encoding utf8
$timings | Format-Table -AutoSize
Write-Output 'Overhead ratios are measurements, not a shared-runner absolute latency gate.'

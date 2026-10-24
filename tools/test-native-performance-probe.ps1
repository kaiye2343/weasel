param([ValidateSet('x64', 'Win32')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch = Join-Path $project ('.tools/native-performance-probe-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$objectDir = Join-Path $scratch 'obj'
New-Item -ItemType Directory -Path $objectDir -Force | Out-Null
$exe = Join-Path $scratch 'native_performance_probe_test.exe'
$reportPath = Join-Path $scratch 'report.json'
$arguments = @('/nologo', '/EHsc', '/std:c++17', '/W4', '/WX', '/utf-8', '/MT', '/O2', '/Y-',
  "/I$project/include", "/Fo$objectDir/")
& cl @arguments (Join-Path $project 'tests/native_performance_probe_test.cpp') "/Fe:$exe"
if ($LASTEXITCODE -ne 0) { throw "Performance probe compilation failed: $LASTEXITCODE" }
& $exe $reportPath
if ($LASTEXITCODE -ne 0) { throw "Performance probe test failed: $LASTEXITCODE" }
if (!(Test-Path -LiteralPath $reportPath)) { throw 'Performance probe fixture did not write a report' }
$report = Get-Content -LiteralPath $reportPath -Raw -Encoding utf8 | ConvertFrom-Json -AsHashtable
function RequireKeys([System.Collections.IDictionary]$value, [string[]]$expected) {
  if (!$value -or (Compare-Object @($value.Keys | Sort-Object) @($expected | Sort-Object))) {
    throw 'Unexpected performance report fields'
  }
}
function RequireNumber($value) {
  if ($null -eq $value -or $value -is [string] -or $value -is [bool] -or
      ![double]::IsFinite([double]$value) -or [double]$value -lt 0) { throw 'Invalid report number' }
}
RequireKeys $report @('schemaVersion', 'sourceRevision', 'pid', 'bitness', 'backend', 'dpi',
  'scenarioId', 'requestedDurationSeconds', 'durationMs', 'stages', 'counters')
if ($report.schemaVersion -ne 1 -or $report.sourceRevision -cnotmatch '^(unknown|[0-9a-fA-F]{40})$' -or
    $report.scenarioId -cnotmatch '^[a-zA-Z0-9._-]{1,64}$' -or $report.backend -notin @('gdi', 'dcomp', 'warp') -or
    $report.bitness -ne $(if ($Architecture -eq 'x64') { 64 } else { 32 }) -or
    $report.dpi -lt 1 -or $report.dpi -gt 768 -or $report.requestedDurationSeconds -lt 1 -or
    $report.requestedDurationSeconds -gt 120) { throw 'Invalid report metadata' }
RequireNumber $report.durationMs
if ($report.durationMs -gt $report.requestedDurationSeconds * 1000) { throw 'Unbounded report duration' }
$stages = @('core_process_key', 'context_response', 'ipc_roundtrip', 'layout', 'resource_prepare',
  'paint_submit', 'present_complete')
RequireKeys $report.stages $stages
foreach ($name in $stages) {
  $stage = $report.stages[$name]
  RequireKeys $stage @('status', 'samples', 'dropped', 'p50Ms', 'p95Ms', 'maxMs')
  RequireNumber $stage.samples
  RequireNumber $stage.dropped
  if ($stage.samples -gt 65536 -or [math]::Truncate($stage.samples) -ne $stage.samples -or
      [math]::Truncate($stage.dropped) -ne $stage.dropped) { throw 'Unbounded sample count' }
  if ($stage.status -eq 'unavailable') {
    if ($stage.samples -ne 0 -or $stage.dropped -ne 0 -or $null -ne $stage.p50Ms -or
        $null -ne $stage.p95Ms -or $null -ne $stage.maxMs) { throw 'Unavailable stage contains fabricated measurements' }
  } else {
    if ($stage.status -notin @('empty', 'measured') -or
        ($stage.status -eq 'empty') -ne ($stage.samples -eq 0)) { throw 'Invalid stage status' }
    RequireNumber $stage.p50Ms
    RequireNumber $stage.p95Ms
    RequireNumber $stage.maxMs
    if ($stage.p50Ms -gt $stage.p95Ms -or $stage.p95Ms -gt $stage.maxMs -or
        ($stage.status -eq 'empty' -and $stage.maxMs -ne 0)) { throw 'Invalid stage percentiles' }
  }
}
RequireKeys $report.counters @('file_metadata', 'digest_read', 'decode', 'scale', 'background_rebuild', 'frame_submit')
foreach ($value in $report.counters.Values) { RequireNumber $value }
# Check include compatibility with the Windows min/max macros used by the native endpoints.
$compatSource = Join-Path $scratch 'windows_compat.cpp'
[IO.File]::WriteAllText($compatSource, @'
#include <windows.h>
#include <WeaselPerformance.h>
int main() {
  weasel::performance::Scope scope(weasel::performance::GetProcessProbe(), weasel::performance::Stage::Layout);
  return weasel::performance::GetProcessProbe().Enabled() ? 1 : 0;
}
'@, [Text.UTF8Encoding]::new($false))
$compatExe = Join-Path $scratch 'windows_compat.exe'
& cl @arguments $compatSource "/Fe:$compatExe"
if ($LASTEXITCODE -ne 0) { throw "Performance probe Windows header compatibility failed: $LASTEXITCODE" }
& $compatExe
if ($LASTEXITCODE -ne 0) { throw 'Performance probe was enabled without explicit activation' }
# Exercise the actual process endpoint, not only explicitly constructed locals.
# Keep the default non-/O2 mode that exposed the missing inline initialization.
foreach ($mode in @('default', 'optimized', 'production-default', 'production-optimized')) {
  $lifetimeExe = Join-Path $scratch "process-probe-$mode.exe"
  $lifetimeFlags = @('/nologo', '/EHsc', '/std:c++17', '/W4', '/WX', '/utf-8', '/MT', '/Y-',
    "/I$project/include", "/Fo$objectDir/")
  if ($mode.EndsWith('optimized')) { $lifetimeFlags += '/O2' }
  if ($mode.StartsWith('production-')) { $lifetimeFlags += '/Zc:threadSafeInit-' }
  & cl @lifetimeFlags (Join-Path $project 'tests/process_performance_probe_test.cpp') (Join-Path $project 'tests/process_performance_probe_peer.cpp') "/Fe:$lifetimeExe"
  if ($LASTEXITCODE -ne 0) { throw "Process probe lifetime compilation failed: $mode" }
  & $lifetimeExe
  if ($LASTEXITCODE -ne 0) { throw "Process probe lifetime regression failed: $mode" }
}
$archive = Join-Path $project 'output/archives'
New-Item -ItemType Directory -Path $archive -Force | Out-Null
Copy-Item -LiteralPath $reportPath -Destination (Join-Path $archive "performance-core-$Architecture.json")
Write-Output "PASS bounded performance core and JSON contract ($Architecture); synthetic data only"
Write-Output "Fixture retained for review: $scratch"

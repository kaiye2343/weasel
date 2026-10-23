$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$archiveRoot = Join-Path $project 'output/archives'
New-Item -ItemType Directory -Path $archiveRoot -Force | Out-Null
$logs = @(
  [pscustomobject]@{ path = 'msbuild1.log'; architecture = 'Win32'; origin = 'production'; control = '' },
  [pscustomobject]@{ path = 'msbuild2.log'; architecture = 'x64'; origin = 'production'; control = '' },
  [pscustomobject]@{ path = 'msbuild-preview-Win32.log'; architecture = 'Win32'; origin = 'production'; control = '' },
  [pscustomobject]@{ path = 'msbuild-preview-x64.log'; architecture = 'x64'; origin = 'production'; control = '' }
)
$controlGroups = @(Get-ChildItem -LiteralPath (Join-Path $project '.tools') -Directory -Filter 'native-baseline-*' |
  Where-Object {
    (Test-Path -LiteralPath (Join-Path $_.FullName 'old-algorithms/control-build.log') -PathType Leaf) -and
    (Test-Path -LiteralPath (Join-Path $_.FullName 'old-placement/control-build.log') -PathType Leaf)
  })
if ($controlGroups.Count -ne 1) { throw 'Expected exactly one complete historical-control log group' }
foreach ($control in @('old-algorithms', 'old-placement')) {
  $path = Join-Path $controlGroups[0].FullName "$control/control-build.log"
  $logs += [pscustomobject]@{
    path = [IO.Path]::GetRelativePath($project, $path).Replace('\', '/')
    architecture = 'x64'
    origin = 'negative-control'
    control = $control
  }
}
$inputPath = Join-Path $project '.tools/native-diagnostics-inputs.json'
@{ schemaVersion = 1; logs = $logs } | ConvertTo-Json -Depth 5 |
  Set-Content -LiteralPath $inputPath -Encoding utf8
$reportPath = Join-Path $archiveRoot 'build-diagnostics.json'
$summaryPath = Join-Path $archiveRoot 'build-diagnostics.md'
& node (Join-Path $PSScriptRoot 'check-native-diagnostics.cjs') --root $project --inputs $inputPath `
  --baseline (Join-Path $PSScriptRoot 'native-warning-baseline.json') --output $reportPath --summary $summaryPath
$diagnosticExit = $LASTEXITCODE
if (Test-Path -LiteralPath $summaryPath) {
  Get-Content -LiteralPath $summaryPath
  if ($env:GITHUB_STEP_SUMMARY) {
    Get-Content -LiteralPath $summaryPath -Raw | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding utf8
  }
}
if ($diagnosticExit -ne 0) { throw "Native diagnostics gate failed: $diagnosticExit" }
$rawRoot = Join-Path $archiveRoot 'build-logs'
New-Item -ItemType Directory -Path $rawRoot -Force | Out-Null
foreach ($log in $logs) {
  $name = if ($log.origin -eq 'negative-control') { $log.control + '.log' } else { [IO.Path]::GetFileName($log.path) }
  Copy-Item -LiteralPath (Join-Path $project $log.path) -Destination (Join-Path $rawRoot $name)
}
Write-Output 'PASS native production and historical-control diagnostic gate'

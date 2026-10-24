$ErrorActionPreference = 'Stop'
# The CLI creates only a memory DC; it never creates a candidate HWND/session.
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$inputPath = Join-Path $project 'tests/fixtures/skin-anchor-layout-v1.json'
$archive = Join-Path $project 'output/archives'
New-Item -ItemType Directory -Path $archive -Force | Out-Null
foreach ($target in @(@('x64','output/WeaselSkinPreview.exe'), @('Win32','output/Win32/WeaselSkinPreview.exe'))) {
  $report = Join-Path $archive "native-text-facts-$($target[0]).json"
  Remove-Item -LiteralPath $report -ErrorAction SilentlyContinue
  $arguments = @('--layout-facts', ('"' + $inputPath + '"'), ('"' + $report + '"'))
  $process = Start-Process -FilePath (Join-Path $project $target[1]) -ArgumentList $arguments -WindowStyle Hidden -PassThru
  try {
    if (!$process.WaitForExit(60000)) { $process.Kill(); throw 'Native layout facts timed out' }
    if ($process.ExitCode -ne 0) { throw "Native layout facts failed ($($target[0])): $($process.ExitCode)" }
  } finally { $process.Dispose() }
  & node (Join-Path $PSScriptRoot 'native-layout-facts.cjs') $report (Join-Path $archive "native-text-comparison-$($target[0]).json")
  if ($LASTEXITCODE -ne 0) { throw 'Native fixed-metric layout comparison failed' }
}

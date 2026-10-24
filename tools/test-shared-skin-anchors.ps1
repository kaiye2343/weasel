param([ValidateSet('x64', 'Win32')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch = Join-Path $project ('.tools/shared-skin-anchors-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$generated = Join-Path $scratch 'shared_skin_anchor_cases.inc'
& node (Join-Path $PSScriptRoot 'shared-skin-anchor-fixture.cjs') generate $generated
if ($LASTEXITCODE -ne 0) { throw 'Shared anchor fixture generation failed' }
$bits = if ($Architecture -eq 'x64') { 64 } else { 32 }
$exe = Join-Path $scratch 'shared_skin_anchor_test.exe'
& cl /nologo /EHsc /std:c++17 /O2 /W4 /WX /utf-8 /MT /Y- "/DANCHOR_EXPECTED_BITS=$bits" "/I$scratch" (Join-Path $project 'tests/shared_skin_anchor_test.cpp') "/Fo$scratch/anchors.obj" "/Fe:$exe"
if ($LASTEXITCODE -ne 0) { throw 'Shared native anchor fixture compilation failed' }
$report = Join-Path $scratch 'geometry.json'
$errorLog = Join-Path $scratch 'geometry.stderr.log'
$process = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru -RedirectStandardOutput $report -RedirectStandardError $errorLog
if (!$process.WaitForExit(30000)) {
  $process.Kill()
  throw 'Isolated native anchor fixture timed out'
}
if ($process.ExitCode -ne 0) {
  Get-Content -LiteralPath $errorLog
  throw "Isolated native anchor fixture failed: $($process.ExitCode)"
}
& node (Join-Path $PSScriptRoot 'shared-skin-anchor-fixture.cjs') compare $report
if ($LASTEXITCODE -ne 0) { throw 'Shared native/browser anchor comparison failed' }
$archive = Join-Path $project 'output/archives'
New-Item -ItemType Directory -Path $archive -Force | Out-Null
Copy-Item -LiteralPath $report -Destination (Join-Path $archive "shared-skin-anchors-$Architecture.json")
Write-Output "PASS shared native/browser decoration anchors ($Architecture); text layout, hit testing and real fonts remain outside this batch"

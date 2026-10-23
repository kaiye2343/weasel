$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch = Join-Path $project ('.tools/symbol-parser-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$include = Join-Path $project 'tools/symbols'
$source = Join-Path $project 'tests/pe_codeview_test.cpp'
$exe = Join-Path $scratch 'pe_codeview_test.exe'
& cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /Y- "/I$include" $source "/Fo$scratch/" "/Fe:$exe"
if ($LASTEXITCODE -ne 0) {
  throw "PE CodeView test compilation failed: $LASTEXITCODE"
}
& $exe
if ($LASTEXITCODE -ne 0) {
  throw "PE CodeView test failed: $LASTEXITCODE"
}
Write-Output "Fixture retained for review: $scratch"
Write-Output 'PASS PE CodeView identity parser'

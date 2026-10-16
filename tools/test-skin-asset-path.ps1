$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testRoot = Join-Path $workspace ('.tools/skin-path-' + [guid]::NewGuid().ToString('N'))
$physical = Join-Path $testRoot 'physical'
$alias = Join-Path $testRoot 'view'
New-Item -ItemType Directory -Path $physical -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $workspace 'skins/shorekeeper/shorekeeper-watermelon.png') -Destination (Join-Path $physical 'skin.png')
foreach ($baseline in @($true, $false)) {
    New-Item -ItemType Junction -Path $alias -Target $physical | Out-Null
    $binary = Join-Path $testRoot $(if ($baseline) { 'baseline.exe' } else { 'repaired.exe' })
    $compilerArgs = @('/nologo', '/EHsc', '/std:c++17', '/W4', (Join-Path $workspace 'tests/skin_asset_path_test.cpp'), "/Fe:$binary", "/Fo:$testRoot/test.obj")
    if ($baseline) { $compilerArgs += '/DSKIN_PATH_BASELINE' }
    & cl @compilerArgs
    if ($LASTEXITCODE -ne 0) { throw 'Skin asset path test compilation failed' }
    & $binary (Join-Path $alias 'skin.png') (Join-Path $physical 'skin.png')
    $expected = if ($baseline) { 4 } else { 0 }
    if ($LASTEXITCODE -ne $expected) { throw "Path-view test returned $LASTEXITCODE, expected $expected" }
}
exit 0

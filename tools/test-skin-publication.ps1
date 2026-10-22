param([string]$OutputRoot = '.tools/skin-publication')
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$run = Join-Path ([IO.Path]::GetFullPath((Join-Path $project $OutputRoot))) ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run -Force | Out-Null
Push-Location $project
try {
    foreach ($name in @('skin_asset_publisher_test', 'skin_asset_container_test', 'skin_style_transaction_test')) {
        $exe = Join-Path $run "$name.exe"
        & cl /nologo /EHsc /MT /std:c++17 /utf-8 /W4 /DNOMINMAX /D_WIN32_WINNT=0x0A00 "tests/$name.cpp" WeaselUI/SkinAssetPublisher.cpp "/Fe:$exe" "/Fo:$run/" 2>&1 | Tee-Object -FilePath (Join-Path $run "$name-build.log")
        if ($LASTEXITCODE -ne 0) { throw "$name compilation failed" }
        & $exe (Join-Path $project 'skins/shorekeeper/shorekeeper-watermelon.png') (Join-Path $run "$name-fixture") 2>&1 | Tee-Object -FilePath (Join-Path $run "$name.log")
        if ($LASTEXITCODE -ne 0) { throw "$name failed" }
    }
    Write-Output "PASS publication and real AppContainer regressions: $run"
} finally { Pop-Location }

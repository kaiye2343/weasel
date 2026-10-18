param([Parameter(Mandatory)][string]$SourceRoot,
      [Parameter(Mandatory)][string]$OutputRoot,
      [Parameter(Mandatory)][string]$BoostRoot,
      [switch]$Fork)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $SourceRoot).Path
$driver = (Resolve-Path -LiteralPath "$PSScriptRoot/../tests/frontend_comparison_benchmark.cpp").Path
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$output = (Resolve-Path -LiteralPath $OutputRoot).Path
$arguments = @('/nologo','/EHsc','/O2','/MT','/std:c++17','/utf-8','/DUNICODE','/D_UNICODE',
               '/DBOOST_JSON_NO_LIB',"/I$source/include","/I$source/WeaselUI","/I$BoostRoot",
               "/Fe:$output/frontend-comparison.exe", "/Fo:$output/")
if ($Fork) { $arguments += '/DWEASEL_FORK_BENCHMARK' }
$files = @('DirectWriteResources','FullScreenLayout','GdiplusBlur','HorizontalLayout',
           'Layout','StandardLayout','VerticalLayout','VHorizontalLayout','WeaselPanel','WeaselUI') |
    ForEach-Object { "$source/WeaselUI/$_.cpp" }
if ($Fork) {
    $jsonSource = Join-Path $output 'boost-json.cpp'
    '#include <boost/json/src.hpp>' | Set-Content -LiteralPath $jsonSource -Encoding utf8
    $files += "$source/WeaselUI/SkinPackage.cpp", $jsonSource
}
& cl @arguments $driver @files /link user32.lib gdi32.lib advapi32.lib ole32.lib shell32.lib uuid.lib
if ($LASTEXITCODE -ne 0) { throw 'Frontend comparison compilation failed' }

$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch = Join-Path $project ('.tools/upstream-adaptations-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Extract([string]$source, [string]$start, [string]$end) {
  $a = $source.IndexOf($start, [StringComparison]::Ordinal)
  if ($a -lt 0) { throw "Missing production test seam: $start" }
  $b = $source.IndexOf($end, $a + $start.Length, [StringComparison]::Ordinal)
  if ($b -lt 0) { throw "Missing production test seam: $end" }
  return $source.Substring($a, $b - $a)
}
function ResponseSource([string]$source) {
  $candidate = Extract $source 'void RimeWithWeaselHandler::_GetCandidateInfo(' 'void RimeWithWeaselHandler::StartMaintenance()'
  $response = Extract $source 'inline std::string _GetLabelText(' 'static inline COLORREF blend_colors('
  return $candidate + "`n" + $response
}
function CheckExe([string]$exe, [int]$expected = 0) {
  & $exe
  if ($LASTEXITCODE -ne $expected) { throw "$exe expected $expected, got $LASTEXITCODE" }
}
function CompileResponse([string]$name, [string]$source, [bool]$legacy) {
  $directory = Join-Path $scratch $name
  New-Item -ItemType Directory -Path $directory | Out-Null
  [IO.File]::WriteAllText((Join-Path $directory 'response_under_test.inc'), (ResponseSource $source), $utf8)
  $flags = @('/nologo', '/EHsc', '/std:c++17', '/utf-8', '/MT', '/DUNICODE', '/D_UNICODE', '/DBOOST_USE_WINAPI_VERSION=0x0603', "/I$directory", "/I$project/include", "/I$env:BOOST_ROOT", '/Y-')
  if ($legacy) { $flags += '/DLEGACY_RESPONSE' }
  $sources = @('tests/upstream_response_test.cpp', 'WeaselIPC/ResponseParser.cpp', 'WeaselIPC/Deserializer.cpp', 'WeaselIPC/ActionLoader.cpp', 'WeaselIPC/Committer.cpp', 'WeaselIPC/ContextUpdater.cpp', 'WeaselIPC/Configurator.cpp', 'WeaselIPC/Styler.cpp')
  $exe = Join-Path $directory 'response.exe'
  & cl @flags @sources "/Fo$directory/" "/Fe:$exe" /link "/LIBPATH:$env:BOOST_ROOT/stage/lib" advapi32.lib
  if ($LASTEXITCODE -ne 0) { throw "Response fixture compilation failed: $name" }
  CheckExe $exe
}
$responsePath = Join-Path $project 'RimeWithWeasel/RimeWithWeasel.cpp'
CompileResponse 'current' ([IO.File]::ReadAllText($responsePath)) $false
# The same client expectations must also pass on the pre-optimization responder.
$baseline = git -C $project show 'f701308^:RimeWithWeasel/RimeWithWeasel.cpp'
if ($LASTEXITCODE -ne 0) { throw 'Missing response baseline in Git history' }
CompileResponse 'baseline' ($baseline -join "`n") $true
$panel = [IO.File]::ReadAllText((Join-Path $project 'WeaselUI/WeaselPanel.cpp'))
$hover = Extract $panel 'LRESULT WeaselPanel::OnMouseMove(' 'LRESULT WeaselPanel::OnMouseLeave('
[IO.File]::WriteAllText((Join-Path $scratch 'hover_under_test.inc'), $hover, $utf8)
& cl /nologo /EHsc /std:c++17 /utf-8 /MT /DUNICODE /D_UNICODE "/I$project/include" "/I$env:BOOST_ROOT" "/I$scratch" tests/upstream_hover_test.cpp "/Fo$scratch/hover.obj" "/Fe:$scratch/hover.exe" /link user32.lib
if ($LASTEXITCODE -ne 0) { throw 'Hover fixture compilation failed' }
CheckExe "$scratch/hover.exe"
# Negative control: reproduce the unadapted upstream coordinate conversion.
$wrong = $hover.Replace('CPoint ptScreen(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));', 'CPoint ptScreen = point;')
if ($wrong -eq $hover) { throw 'Hover coordinate negative-control seam changed' }
[IO.File]::WriteAllText((Join-Path $scratch 'hover_under_test.inc'), $wrong, $utf8)
& cl /nologo /EHsc /std:c++17 /utf-8 /MT /DUNICODE /D_UNICODE "/I$project/include" "/I$env:BOOST_ROOT" "/I$scratch" tests/upstream_hover_test.cpp "/Fo$scratch/hover-bad.obj" "/Fe:$scratch/hover-bad.exe" /link user32.lib
if ($LASTEXITCODE -ne 0) { throw 'Hover negative-control compilation failed' }
CheckExe "$scratch/hover-bad.exe" 1
& cl /nologo /EHsc /std:c++17 /utf-8 /DVERSION_MAJOR=0 /DVERSION_MINOR=17 /DVERSION_PATCH=4 /DPRODUCT_VERSION=0.17.4.999.test "/I$project/include" tests/version_macro_test.cpp "/Fo$scratch/version.obj" "/Fe:$scratch/version.exe"
if ($LASTEXITCODE -ne 0) { throw 'Product version expansion test failed' }
CheckExe "$scratch/version.exe"
Write-Output 'PASS upstream adaptations: real response/client, baseline semantics, hover with negative control, product version'

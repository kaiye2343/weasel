$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch = Join-Path $project ('.tools/native-lifetimes-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Extract([string]$source, [string]$start, [string]$end) {
  $a = $source.IndexOf($start, [StringComparison]::Ordinal)
  if ($a -lt 0) { throw "Missing production test seam: $start" }
  $b = $source.IndexOf($end, $a + $start.Length, [StringComparison]::Ordinal)
  if ($b -lt 0) { throw "Missing production test seam: $end" }
  return $source.Substring($a, $b - $a)
}
$register = [IO.File]::ReadAllText((Join-Path $project 'WeaselTSF/Register.cpp'), $utf8)
$findIme = Extract $register 'HKL FindIME(LANGID langid)' 'BOOL RegisterProfiles()'
[IO.File]::WriteAllText((Join-Path $scratch 'find_ime_under_test.inc'), $findIme, $utf8)
$dialog = [IO.File]::ReadAllText((Join-Path $project 'WeaselDeployer/DictManagementDialog.cpp'), $utf8)
$shellItem = Extract $dialog 'void static OpenFolderAndSelectItem(std::wstring filepath)' 'template <typename T, typename U>'
[IO.File]::WriteAllText((Join-Path $scratch 'shell_item_under_test.inc'), $shellItem, $utf8)
$testSource = Join-Path $project 'tests/native_lifetime_test.cpp'
$exe = Join-Path $scratch 'native_lifetime_test.exe'
$objectDir = Join-Path $scratch 'obj'
New-Item -ItemType Directory -Path $objectDir -Force | Out-Null
& cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /Y- "/I$scratch" $testSource "/Fo$objectDir/" "/Fe:$exe"
if ($LASTEXITCODE -ne 0) { throw "Native lifetime fixture compilation failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Native lifetime fixture failed: $LASTEXITCODE" }
Write-Output "Fixture retained for review: $scratch"
Write-Output 'PASS native lifetime regressions'

$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch = Join-Path $project ('.tools/native-profile-contracts-' + [guid]::NewGuid().ToString('N'))
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
$profiles = Extract $register 'BOOL RegisterProfiles() {' 'void UnregisterProfiles()'
[IO.File]::WriteAllText((Join-Path $scratch 'profiles_under_test.inc'), $profiles, $utf8)
$testSource = Join-Path $project 'tests/native_profile_contract_test.cpp'
$exe = Join-Path $scratch 'native_profile_contract_test.exe'
$objectDir = Join-Path $scratch 'obj'
New-Item -ItemType Directory -Path $objectDir -Force | Out-Null
& cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DUNICODE /D_UNICODE /Y- "/I$scratch" $testSource "/Fo$objectDir/" "/Fe:$exe"
if ($LASTEXITCODE -ne 0) { throw "Native profile contract compilation failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Native profile contract test failed: $LASTEXITCODE" }
Write-Output "Fixture retained for review: $scratch"
Write-Output 'PASS native profile contracts'

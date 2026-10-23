$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch = Join-Path $project ('.tools/native-start-time-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Extract([string]$source, [string]$start, [string]$end) {
  $a = $source.IndexOf($start, [StringComparison]::Ordinal)
  if ($a -lt 0) { throw "Missing production test seam: $start" }
  $b = $source.IndexOf($end, $a + $start.Length, [StringComparison]::Ordinal)
  if ($b -lt 0) { throw "Missing production test seam: $end" }
  return $source.Substring($a, $b - $a)
}
$server = [IO.File]::ReadAllText((Join-Path $project 'WeaselIPCServer/WeaselServerImpl.cpp'), $utf8)
$utility = [IO.File]::ReadAllText((Join-Path $project 'include/WeaselUtility.h'), $utf8)
$startSource = Extract $server 'int ServerImpl::Start()' 'int ServerImpl::Stop()'
$timeSource = Extract $utility 'inline std::string current_time()' '#define DEBUG'
$templatePath = Join-Path $project 'tests/native_start_time_test.cpp'
$template = [IO.File]::ReadAllText($templatePath, $utf8)
foreach ($marker in '// PRODUCTION_START', '// PRODUCTION_TIME') {
  $count = ([regex]::Matches($template, [regex]::Escape($marker))).Count
  if ($count -ne 1) { throw "Fixture marker must appear exactly once: $marker (found $count)" }
}
$fixture = $template.Replace('// PRODUCTION_START', $startSource).Replace('// PRODUCTION_TIME', $timeSource)
$fixturePath = Join-Path $scratch 'fixture.cpp'
[IO.File]::WriteAllText($fixturePath, $fixture, $utf8)
$objectDir = Join-Path $scratch 'obj'
New-Item -ItemType Directory -Path $objectDir -Force | Out-Null
$exe = Join-Path $scratch 'test.exe'
& cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /Y- "/Fo$objectDir/" "/Fe:$exe" $fixturePath
if ($LASTEXITCODE -ne 0) { throw "Native start/time fixture compilation failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Native start/time fixture failed: $LASTEXITCODE" }
Write-Output "Fixture retained for review: $fixturePath"
Write-Output 'PASS native start returns boolean window state and current_time stays stable'

$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch = Join-Path $project ('.tools/native-ipc-bounds-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Extract([string]$source, [string]$start, [string]$end) {
  $a = $source.IndexOf($start, [StringComparison]::Ordinal)
  if ($a -lt 0) { throw "Missing production test seam: $start" }
  $b = $source.IndexOf($end, $a + $start.Length, [StringComparison]::Ordinal)
  if ($b -lt 0) { throw "Missing production test seam: $end" }
  return $source.Substring($a, $b - $a)
}
$ipcHeader = [IO.File]::ReadAllText((Join-Path $project 'include/WeaselIPC.h'), $utf8)
$commands = (Extract $ipcHeader 'enum WEASEL_IPC_COMMAND {' '};') + '};'
[IO.File]::WriteAllText((Join-Path $scratch 'commands_under_test.inc'), $commands, $utf8)
$client = [IO.File]::ReadAllText((Join-Path $project 'WeaselIPC/WeaselClientImpl.cpp'), $utf8)
$candidateMethods = Extract $client 'bool ClientImpl::SelectCandidateOnCurrentPage(' 'bool ClientImpl::ChangePage('
$sessionMethods = Extract $client 'void ClientImpl::StartSession()' 'void ClientImpl::EndSession()'
$echoMethods = Extract $client 'bool ClientImpl::Echo()' 'bool ClientImpl::GetResponseData('
$methods = $candidateMethods + "`n" + $sessionMethods + "`n" + $echoMethods
[IO.File]::WriteAllText((Join-Path $scratch 'client_methods.inc'), $methods, $utf8)
$templatePath = Join-Path $project 'tests/native_ipc_bounds_test.cpp'
$template = [IO.File]::ReadAllText($templatePath, $utf8)
$marker = '// PRODUCTION_CLIENT_METHODS'
$count = ([regex]::Matches($template, [regex]::Escape($marker))).Count
if ($count -ne 1) { throw "Fixture marker must appear exactly once: $marker (found $count)" }
$clientHeader = [IO.File]::ReadAllText((Join-Path $project 'WeaselIPC/WeaselClientImpl.h'), $utf8)
if ($clientHeader -notmatch 'DWORD\s+_SendMessage\s*\(\s*WEASEL_IPC_COMMAND\s+Msg') {
  throw 'ClientImpl::_SendMessage must be declared DWORD in WeaselClientImpl.h'
}
$testSource = Join-Path $project 'tests/native_ipc_bounds_test.cpp'
$pipeSource = Join-Path $project 'WeaselIPC/PipeChannel.cpp'
$exe = Join-Path $scratch 'native_ipc_bounds_test.exe'
$objectDir = Join-Path $scratch 'obj'
New-Item -ItemType Directory -Path $objectDir -Force | Out-Null
& cl /nologo /EHsc /std:c++17 /W4 /WX /utf-8 /MT /DUNICODE /D_UNICODE /DBOOST_USE_WINAPI_VERSION=0x0603 "/I$project/include" "/I$env:BOOST_ROOT" "/I$scratch" /Y- $testSource $pipeSource "/Fo$objectDir/" "/Fe:$exe" /link "/LIBPATH:$env:BOOST_ROOT/stage/lib"
if ($LASTEXITCODE -ne 0) { throw "Native IPC bounds fixture compilation failed: $LASTEXITCODE" }
& $exe
if ($LASTEXITCODE -ne 0) { throw "Native IPC bounds fixture failed: $LASTEXITCODE" }
Write-Output "Fixture retained for review: $scratch"
Write-Output 'PASS native IPC bounds and client conversions'

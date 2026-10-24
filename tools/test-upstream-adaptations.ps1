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
# The installed 0.17.4.64 client can remain loaded in an application during an
# upgrade. Its style archive field sequence must remain identical; the new
# manifest capability is enforced by the server via annotation_position.
$oldHeader = (git -C $project show 'e023983:include/WeaselIPCData.h') -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Missing installed protocol baseline' }
$newHeader = [IO.File]::ReadAllText((Join-Path $project 'include/WeaselIPCData.h'))
$styleStart = 'void serialize(Archive& ar, weasel::UIStyle& s,'
$styleEnd = 'template <typename Archive>'
function ArchiveFields([string]$header) {
  $body = Extract $header $styleStart $styleEnd
  return ([regex]::Matches($body, 's\.[a-zA-Z_][a-zA-Z_0-9]*') | ForEach-Object { $_.Value }) -join ','
}
if ((ArchiveFields $oldHeader) -ne (ArchiveFields $newHeader)) { throw 'UIStyle archive fields differ from installed-client baseline' }
Write-Output 'PASS UIStyle archive field order matches installed 0.17.4.64 clients'
function CheckExe([string]$exe, [int]$expected = 0) {
  $log = $exe + '.stdout.log'
  $errorLog = $exe + '.stderr.log'
  $process = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError $errorLog
  if (!$process.WaitForExit(30000)) {
    $process.Kill()
    throw "Isolated upstream regression timed out: $exe"
  }
  Get-Content -LiteralPath $log
  Get-Content -LiteralPath $errorLog
  if ($process.ExitCode -ne $expected) { throw "$exe expected $expected, got $($process.ExitCode)" }
}
function CompileResponse([string]$name, [string]$source, [bool]$legacy, [bool]$diagnostic = $false) {
  $directory = Join-Path $scratch $name
  New-Item -ItemType Directory -Path $directory | Out-Null
  [IO.File]::WriteAllText((Join-Path $directory 'response_under_test.inc'), (ResponseSource $source), $utf8)
  $flags = @('/nologo', '/EHsc', '/std:c++17', '/utf-8', '/MT', '/DUNICODE', '/D_UNICODE', '/DBOOST_USE_WINAPI_VERSION=0x0603', "/I$directory", "/I$project/include", "/I$env:BOOST_ROOT", '/Y-')
  if ($legacy) { $flags += '/DLEGACY_RESPONSE' }
  if ($diagnostic) { $flags += @('/Zi', '/Bv', '/DWEASEL_RESPONSE_FAULT_DIAGNOSTICS', "/Fd$directory/response-compile.pdb") }
  $sources = @('tests/upstream_response_test.cpp', 'WeaselIPC/ResponseParser.cpp', 'WeaselIPC/Deserializer.cpp', 'WeaselIPC/ActionLoader.cpp', 'WeaselIPC/Committer.cpp', 'WeaselIPC/ContextUpdater.cpp', 'WeaselIPC/Configurator.cpp', 'WeaselIPC/Styler.cpp')
  $exe = Join-Path $directory 'response.exe'
  $linkFlags = @("/LIBPATH:$env:BOOST_ROOT/stage/lib", "/MAP:$directory/response.map", 'advapi32.lib', 'user32.lib')
  if ($diagnostic) { $linkFlags += @('/DEBUG:FULL', "/PDB:$directory/response.pdb", 'dbghelp.lib') }
  @{ sourceRevision = (git -C $project rev-parse HEAD); compiler = (Get-Command cl).Source;
     architecture = $env:VSCMD_ARG_TGT_ARCH; flags = $flags; sources = $sources; linkFlags = $linkFlags } |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $directory 'build-command.log') -Encoding utf8
  & cl @flags @sources "/Fo$directory/" "/Fe:$exe" /link @linkFlags
  if ($LASTEXITCODE -ne 0) { throw "Response fixture compilation failed: $name" }
  CheckExe $exe
}
$responsePath = Join-Path $project 'RimeWithWeasel/RimeWithWeasel.cpp'
try {
  CompileResponse 'current' ([IO.File]::ReadAllText($responsePath)) $false
} catch {
  $originalFailure = $_
  # Read the uninstrumented, non-/DEBUG executable in a separate debugger.
  # MAP generation changes no compiler options or target source.
  try {
    $map = [IO.File]::ReadAllText((Join-Path $scratch 'current/response.map'))
    $preferred = [regex]::Match($map, 'Preferred load address is\s+([0-9A-Fa-f]+)')
    $probe = [regex]::Match($map, '(?m)^\s*[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+\?processProbe@performance@weasel@@[^\s]*\s+([0-9A-Fa-f]+)')
    if (!$preferred.Success -or !$probe.Success) { throw 'Missing exact probe MAP address' }
    $rva = [Convert]::ToUInt64($probe.Groups[1].Value,16) - [Convert]::ToUInt64($preferred.Groups[1].Value,16)
    $debugger = Join-Path $scratch 'response_debugger.exe'
    & cl /nologo /EHsc /std:c++17 /utf-8 /MT "/I$project/include" tests/response_debugger.cpp "/Fo$scratch/response_debugger.obj" "/Fe:$debugger" /link dbghelp.lib
    if ($LASTEXITCODE -ne 0) { throw 'Passive response debugger compilation failed' }
    & $debugger (Join-Path $scratch 'current/response.exe') $rva.ToString('x') 2>&1 |
      Tee-Object -FilePath (Join-Path $scratch 'passive-debugger.log')
    if ($LASTEXITCODE -ne 0) { Write-Output "Passive debugger did not capture the original AV: $LASTEXITCODE" }
    & dumpbin /DISASM (Join-Path $scratch 'current/response.exe') > (Join-Path $scratch 'original-disassembly.log')
  } catch { Write-Output "Passive debugger evidence unavailable: $_" }
  Write-Output 'Original response fixture failed; collecting an isolated symbolized reproduction without changing optimization or runtime.'
  try { CompileResponse 'current-symbols' ([IO.File]::ReadAllText($responsePath)) $false $true }
  catch { Write-Output "Symbolized reproduction: $_" }
  throw $originalFailure
}
# The same client expectations must also pass on the pre-optimization responder.
$baseline = git -C $project show 'c8c3332^:RimeWithWeasel/RimeWithWeasel.cpp'
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
function CompileAbort([string]$name, [string]$source, [int]$expected) {
  $method = Extract $source 'void WeaselTSF::_AbortComposition(bool clear)' 'void WeaselTSF::_FinalizeComposition()'
  [IO.File]::WriteAllText((Join-Path $scratch 'abort_under_test.inc'), $method, $utf8)
  & cl /nologo /EHsc /std:c++17 /utf-8 /MT "/I$scratch" tests/upstream_abort_test.cpp "/Fo$scratch/$name.obj" "/Fe:$scratch/$name.exe"
  if ($LASTEXITCODE -ne 0) { throw 'Candidate abort test compilation failed' }
  CheckExe "$scratch/$name.exe" $expected
}
$composition = [IO.File]::ReadAllText((Join-Path $project 'WeaselTSF/Composition.cpp'))
CompileAbort 'abort-current' $composition 0
$oldComposition = git -C $project show '17b5c22:WeaselTSF/Composition.cpp'
if ($LASTEXITCODE -ne 0) { throw 'Missing host-termination baseline' }
CompileAbort 'abort-before-fix' ($oldComposition -join "`n") 1
Write-Output 'PASS upstream adaptations: real response/client and optional cursor, baseline semantics, hover, abort lifecycle with negative controls, product version'

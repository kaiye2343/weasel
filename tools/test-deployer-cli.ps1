param([switch]$VerifyPackage)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($VerifyPackage) {
    foreach ($relative in @('output/WeaselDeployer.exe', 'output/Win32/WeaselDeployer.exe')) {
        $bytes = [IO.File]::ReadAllBytes((Join-Path $project $relative))
        $pe = [BitConverter]::ToInt32($bytes, 0x3c)
        $subsystem = [BitConverter]::ToUInt16($bytes, $pe + 24 + 68)
        if ($subsystem -ne 3) { throw "Deployer is not a Console executable: $relative" }
    }
    Write-Output 'PASS packaged x86/x64 deployer Console subsystem'
    exit 0
}
$scratch = Join-Path $project ('.tools/deployer-cli-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
function Extract([string]$text, [string]$start, [string]$end) {
    $a = $text.IndexOf($start, [StringComparison]::Ordinal)
    if ($a -lt 0) { throw "Missing production seam: $start" }
    $b = if ($end) { $text.IndexOf($end, $a + $start.Length, [StringComparison]::Ordinal) } else { $text.Length }
    if ($b -lt 0) { throw "Missing production end seam: $end" }
    $text.Substring($a, $b - $a)
}
$config = [IO.File]::ReadAllText((Join-Path $project 'WeaselDeployer/Configurator.cpp'))
$entry = [IO.File]::ReadAllText((Join-Path $project 'WeaselDeployer/WeaselDeployer.cpp'))
$dictionary = (Extract $config 'namespace {' 'static void CreateFileIfNotExist') + (Extract $config 'int Configurator::DictManagement(const' 'int Configurator::SyncUserData()')
$options = (Extract $entry 'static bool IsOption(' 'static int ShowHelp()') + (Extract $entry 'static bool IsDictionaryCli(' 'static void DetachGuiConsole()')
$dispatch = $options + (Extract $entry 'static int Run(int argc, wchar_t* argv[]) {' '')
$encoding = Extract $entry '  if (IsDictionaryCli(argc, argv)) {' '  HINSTANCE hInstance'
foreach ($item in @(@('dictionary_under_test.inc', $dictionary), @('dispatch_under_test.inc', $dispatch), @('console_options_under_test.inc', $options), @('console_setup_under_test.inc', $encoding))) {
    [IO.File]::WriteAllText((Join-Path $scratch $item[0]), $item[1], $utf8)
}
foreach ($name in @('deployer_dictionary_test', 'deployer_dictionary_real_test', 'deployer_console_test')) {
    & cl /nologo /EHsc /std:c++17 /utf-8 /MT /DUNICODE /D_UNICODE /DNOMINMAX "/I$scratch" "/I$project/include" "tests/$name.cpp" "/Fo$scratch/$name.obj" "/Fe:$scratch/$name.exe" /link user32.lib
    if ($LASTEXITCODE -ne 0) { throw "CLI regression compile failed: $name" }
}
function Check([string]$exe, [string[]]$arguments, [int]$expected = 0) {
    $start = [Diagnostics.ProcessStartInfo]::new($exe)
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8; $start.StandardErrorEncoding = $utf8
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    $out = $process.StandardOutput.ReadToEndAsync(); $err = $process.StandardError.ReadToEndAsync()
    if (!$process.WaitForExit(60000)) { $process.Kill(); throw 'Isolated CLI regression timed out' }
    $stdout = $out.GetAwaiter().GetResult(); $stderr = $err.GetAwaiter().GetResult()
    [IO.File]::WriteAllText($exe + '.stdout.log', $stdout, $utf8)
    [IO.File]::WriteAllText($exe + '.stderr.log', $stderr, $utf8)
    if ($process.ExitCode -ne $expected) { throw "$exe returned $($process.ExitCode): $stderr" }
    $process.Dispose()
    return $stdout
}
Check "$scratch/deployer_dictionary_test.exe" @()
# Prove the failure assertions detect upstream's ignored initialization result.
$baseline = (git -C $project show 'ba38bad:WeaselDeployer/Configurator.cpp') -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Missing CLI pre-adaptation baseline' }
$oldDictionary = (Extract $baseline 'namespace {' 'static void CreateFileIfNotExist') + (Extract $baseline 'int Configurator::DictManagement(const' 'int Configurator::SyncUserData()')
[IO.File]::WriteAllText((Join-Path $scratch 'dictionary_under_test.inc'), $oldDictionary, $utf8)
& cl /nologo /EHsc /std:c++17 /utf-8 /MT /DUNICODE /D_UNICODE /DNOMINMAX "/I$scratch" "/I$project/include" tests/deployer_dictionary_test.cpp "/Fo$scratch/baseline.obj" "/Fe:$scratch/baseline.exe" /link user32.lib
if ($LASTEXITCODE -ne 0) { throw 'CLI negative control compile failed' }
Check "$scratch/baseline.exe" @() 1
if (!(Get-Content "$scratch/baseline.exe.stderr.log" -Raw).Contains('failure not reported')) { throw 'CLI negative control failed for an unexpected reason' }
[IO.File]::WriteAllText((Join-Path $scratch 'dictionary_under_test.inc'), $dictionary, $utf8)
Write-Output 'PASS negative control detects ignored installation_update failure'
# The real DLL is loaded by absolute path; production mutex/IPC stay substituted.
Check "$scratch/deployer_dictionary_real_test.exe" @((Join-Path $project 'output/rime.dll'), (Join-Path $scratch 'real-rime'))
$unicodePath = 'C:\测试 资料\词典.txt'
$output = Check "$scratch/deployer_console_test.exe" @('/dict', 'export', $unicodePath)
if ($output -notmatch '工作词典' -or !$output.Contains($unicodePath)) { throw 'UTF-8 output or spaced Unicode argument was corrupted' }
if (!(Get-Content "$scratch/deployer_console_test.exe.stderr.log" -Raw).Contains('诊断信息')) { throw 'UTF-8 stderr was corrupted' }
Write-Output 'PASS production console UTF-8 stdout/stderr and CRT Unicode argument boundaries'

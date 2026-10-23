param([switch]$VerifyProduction)

$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch = Join-Path $project ('.tools/native-symbols-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null

if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
  throw 'cl is not available; run in a VS x64 developer environment'
}
$targetArch = [string]$env:VSCMD_ARG_TGT_ARCH
if ($targetArch -notin @('x64', 'amd64')) {
  throw "VSCMD_ARG_TGT_ARCH must be x64 or amd64, actual: '$targetArch'"
}
if ([string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
  throw 'VSINSTALLDIR is not set'
}
$diaRoot = Join-Path $env:VSINSTALLDIR 'DIA SDK'
$diaInclude = Join-Path $diaRoot 'include'
$diaLibDir = Join-Path $diaRoot 'lib/amd64'
$diaBinDir = Join-Path $diaRoot 'bin/amd64'
$dia2Header = Join-Path $diaInclude 'dia2.h'
$diaLib = Join-Path $diaLibDir 'diaguids.lib'
$diaDll = Join-Path $diaBinDir 'msdia140.dll'
foreach ($path in @($dia2Header, (Join-Path $diaInclude 'cvconst.h'), $diaLib, $diaDll)) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "DIA SDK file is missing: $path"
  }
}

$validatorSource = Join-Path $project 'tools/symbols/validate_native_symbols.cpp'
$validator = Join-Path $scratch 'validate_native_symbols.exe'
$validatorArgs = @(
  '/nologo', '/EHsc', '/std:c++17', '/utf-8', '/W4', '/WX', '/MT', '/DNOMINMAX',
  "/I$diaInclude", "/I$project/tools/symbols", "/Fo$scratch/", "/Fe:$validator",
  $validatorSource, '/link', "/LIBPATH:$diaLibDir", 'diaguids.lib', 'ole32.lib', 'oleaut32.lib'
)
& cl @validatorArgs
if ($LASTEXITCODE -ne 0) {
  throw "validator compilation failed: $LASTEXITCODE"
}

$fixtureSource = Join-Path $project 'tests/native_symbol_fixture.cpp'
function Build-Fixture([string]$Name) {
  $dir = Join-Path $scratch $Name
  New-Item -ItemType Directory -Path $dir -Force | Out-Null
  $fixtureArgs = @(
    '/nologo', '/EHsc', '/std:c++17', '/utf-8', '/W4', '/WX', '/MT', '/Zi', '/Od',
    "/Fd$dir/compiler.pdb", "/Fo$dir/", "/Fe:$dir/fixture.exe",
    $fixtureSource, '/link', '/DEBUG:FULL', '/INCREMENTAL:NO', "/PDB:$dir/fixture.pdb"
  )
  & cl @fixtureArgs
  if ($LASTEXITCODE -ne 0) {
    throw "fixture compilation failed for ${Name}: $LASTEXITCODE"
  }
  foreach ($file in @("$dir/fixture.exe", "$dir/fixture.pdb", "$dir/compiler.pdb")) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or
        (Get-Item -LiteralPath $file -Force).Length -le 0) {
      throw "fixture output is missing or empty: $file"
    }
  }
}

Build-Fixture 'sample-a'
Build-Fixture 'sample-b'

$sampleAExe = Join-Path $scratch 'sample-a/fixture.exe'
$sampleAPdb = Join-Path $scratch 'sample-a/fixture.pdb'
$sampleBExe = Join-Path $scratch 'sample-b/fixture.exe'
$sampleBPdb = Join-Path $scratch 'sample-b/fixture.pdb'

function Invoke-SymbolCheck {
  param(
    [string]$Name,
    [string]$Binary,
    [string]$Pdb,
    [string]$Dia,
    [bool]$ExpectSuccess,
    [string]$ExpectedStage = '',
    [uint16]$ExpectedMachine = 0x8664
  )
  $output = @(& $validator $Binary $Pdb $Dia)
  $exitCode = $LASTEXITCODE
  $jsonPath = Join-Path $scratch ($Name + '.json')
  $jsonText = ($output -join "`n")
  [IO.File]::WriteAllText($jsonPath, $jsonText + "`n", [Text.UTF8Encoding]::new($false))
  try {
    $result = ConvertFrom-Json -InputObject $jsonText -ErrorAction Stop
  } catch {
    throw "Invalid JSON for ${Name}: $($_.Exception.Message)"
  }
  if ($result.schemaVersion -isnot [int] -and $result.schemaVersion -isnot [long]) {
    throw "Invalid schemaVersion type for $Name"
  }
  if ([int64]$result.schemaVersion -ne 1) {
    throw "Invalid schemaVersion for $Name"
  }
  if ($result.ok -isnot [bool]) {
    throw "Invalid ok flag for $Name"
  }
  if ($ExpectSuccess) {
    if ($exitCode -ne 0) {
      throw "Expected success exit code for $Name, actual $exitCode"
    }
    if ($result.ok -ne $true) {
      throw "Expected ok=true for $Name"
    }
    if ($result.machine -ne $ExpectedMachine) {
      throw "Expected machine 0x$('{0:X4}' -f $ExpectedMachine) for $Name, actual $($result.machine)"
    }
    if ($result.guid -isnot [string] -or [string]::IsNullOrWhiteSpace([string]$result.guid)) {
      throw "Missing guid for $Name"
    }
    if ([int64]$result.age -le 0) {
      throw "Invalid age for $Name"
    }
    foreach ($field in @('compilands', 'functions', 'lines', 'types')) {
      if ([uint64]$result.$field -eq 0) {
        throw "Expected positive $field for $Name"
      }
    }
  } else {
    if ($exitCode -ne 1) {
      throw "Expected failure exit code for $Name, actual $exitCode"
    }
    if ($result.ok -ne $false) {
      throw "Expected ok=false for $Name"
    }
    if ($result.stage -cne $ExpectedStage) {
      throw "Expected stage '$ExpectedStage' for $Name, actual '$($result.stage)'"
    }
    if ($null -eq $result.hresult) {
      throw "Missing hresult for $Name"
    }
  }
  return $result
}

$truncatedPdb = Join-Path $scratch 'truncated.pdb'
$sourceStream = [IO.File]::OpenRead($sampleAPdb)
try {
  $buffer = [byte[]]::new(128)
  $read = $sourceStream.Read($buffer, 0, 128)
  if ($read -ne 128) {
    throw "Could not read 128 bytes from $sampleAPdb"
  }
  $targetStream = [IO.File]::Open($truncatedPdb, [IO.FileMode]::Create, [IO.FileAccess]::Write)
  try {
    $targetStream.Write($buffer, 0, $read)
  } finally {
    $targetStream.Dispose()
  }
} finally {
  $sourceStream.Dispose()
}

$truncatedExe = Join-Path $scratch 'truncated.exe'
[IO.File]::WriteAllBytes($truncatedExe, [byte[]]@(0x4d, 0x5a))

$regressionResults = @()
try {
  $sampleAResult = Invoke-SymbolCheck -Name 'valid-sample-a' -Binary $sampleAExe -Pdb $sampleAPdb -Dia $diaDll -ExpectSuccess $true
  $regressionResults += [pscustomobject]@{ name = 'valid-sample-a'; expectedStage = ''; actual = $sampleAResult }
  Write-Host 'PASS valid-sample-a'

  $sampleBResult = Invoke-SymbolCheck -Name 'valid-sample-b' -Binary $sampleBExe -Pdb $sampleBPdb -Dia $diaDll -ExpectSuccess $true
  $regressionResults += [pscustomobject]@{ name = 'valid-sample-b'; expectedStage = ''; actual = $sampleBResult }
  Write-Host 'PASS valid-sample-b'

  if ([string]$sampleAResult.guid -eq [string]$sampleBResult.guid) {
    throw 'sample-a and sample-b GUIDs must differ'
  }

  # Change only the exact known fixture RSDS age, preserving its GUID and PDB.
  $wrongAgeExe = Join-Path $scratch 'wrong-age.exe'
  $fixtureBytes = [IO.File]::ReadAllBytes($sampleAExe)
  $identityBytes = [byte[]](@(0x52, 0x53, 0x44, 0x53) +
    ([guid]$sampleAResult.guid).ToByteArray() +
    [BitConverter]::GetBytes([uint32]$sampleAResult.age))
  $identityOffsets = @()
  for ($offset = 0; $offset -le $fixtureBytes.Length - $identityBytes.Length; $offset++) {
    if ($fixtureBytes[$offset] -ne 0x52) { continue }
    $matchesIdentity = $true
    for ($i = 1; $i -lt $identityBytes.Length; $i++) {
      if ($fixtureBytes[$offset + $i] -ne $identityBytes[$i]) { $matchesIdentity = $false; break }
    }
    if ($matchesIdentity) { $identityOffsets += $offset }
  }
  if ($identityOffsets.Count -ne 1) { throw 'Expected exactly one fixture RSDS identity' }
  $changedAge = if ([uint32]$sampleAResult.age -eq [uint32]::MaxValue) {
    [uint32]([uint32]$sampleAResult.age - 1)
  } else { [uint32]([uint32]$sampleAResult.age + 1) }
  [BitConverter]::GetBytes($changedAge).CopyTo($fixtureBytes, $identityOffsets[0] + 20)
  [IO.File]::WriteAllBytes($wrongAgeExe, $fixtureBytes)

  $cases = @(
    @{ Name = 'wrong-pdb'; Binary = $sampleAExe; Pdb = $sampleBPdb; Stage = 'load_validate_pdb' },
    @{ Name = 'wrong-age'; Binary = $wrongAgeExe; Pdb = $sampleAPdb; Stage = 'load_validate_pdb' },
    @{ Name = 'missing-pdb'; Binary = $sampleAExe; Pdb = (Join-Path $scratch 'not-present.pdb'); Stage = 'load_validate_pdb' },
    @{ Name = 'truncated-pdb'; Binary = $sampleAExe; Pdb = $truncatedPdb; Stage = 'load_validate_pdb' },
    @{ Name = 'truncated-pe'; Binary = $truncatedExe; Pdb = $sampleAPdb; Stage = 'read_pe' },
    @{ Name = 'missing-dia'; Binary = $sampleAExe; Pdb = $sampleAPdb; Dia = (Join-Path $scratch 'missing-msdia140.dll'); Stage = 'load_dia' },
    @{ Name = 'relative-dia'; Binary = $sampleAExe; Pdb = $sampleAPdb; Dia = 'msdia140.dll'; Stage = 'load_dia' }
  )
  foreach ($case in $cases) {
    $diaPath = if ($case.ContainsKey('Dia')) { $case.Dia } else { $diaDll }
    $result = Invoke-SymbolCheck -Name $case.Name -Binary $case.Binary -Pdb $case.Pdb -Dia $diaPath -ExpectSuccess $false -ExpectedStage $case.Stage
    $regressionResults += [pscustomobject]@{ name = $case.Name; expectedStage = $case.Stage; actual = $result }
    Write-Host "PASS $($case.Name) expected=$($case.Stage) actual=$($result.stage)"
  }
} finally {
  $regressionJson = ConvertTo-Json -InputObject @($regressionResults) -Depth 8
  [IO.File]::WriteAllText((Join-Path $scratch 'regression-results.json'), $regressionJson + "`n", [Text.UTF8Encoding]::new($false))
}

if (-not $VerifyProduction) {
  Write-Host 'PASS native symbol fixture and negative controls'
  return
}

$productionPairs = @(
  [pscustomobject]@{ Name = '01'; Binary = 'output/WeaselServer.exe'; Pdb = 'output/WeaselServer-x64.pdb'; Machine = 0x8664 },
  [pscustomobject]@{ Name = '02'; Binary = 'output/WeaselDeployer.exe'; Pdb = 'output/WeaselDeployer-x64.pdb'; Machine = 0x8664 },
  [pscustomobject]@{ Name = '03'; Binary = 'output/WeaselSkinPreview.exe'; Pdb = 'output/WeaselSkinPreview-x64.pdb'; Machine = 0x8664 },
  [pscustomobject]@{ Name = '04'; Binary = 'output/weaselx64.dll'; Pdb = 'output/WeaselTSF-x64.pdb'; Machine = 0x8664 },
  [pscustomobject]@{ Name = '05'; Binary = 'output/weaselx64.ime'; Pdb = 'output/WeaselIME-x64.pdb'; Machine = 0x8664 },
  [pscustomobject]@{ Name = '06'; Binary = 'output/Win32/WeaselServer.exe'; Pdb = 'output/Win32/WeaselServer-Win32.pdb'; Machine = 0x14c },
  [pscustomobject]@{ Name = '07'; Binary = 'output/Win32/WeaselDeployer.exe'; Pdb = 'output/Win32/WeaselDeployer-Win32.pdb'; Machine = 0x14c },
  [pscustomobject]@{ Name = '08'; Binary = 'output/Win32/WeaselSkinPreview.exe'; Pdb = 'output/Win32/WeaselSkinPreview-Win32.pdb'; Machine = 0x14c },
  [pscustomobject]@{ Name = '09'; Binary = 'output/weasel.dll'; Pdb = 'output/WeaselTSF-Win32.pdb'; Machine = 0x14c },
  [pscustomobject]@{ Name = '10'; Binary = 'output/weasel.ime'; Pdb = 'output/WeaselIME-Win32.pdb'; Machine = 0x14c },
  [pscustomobject]@{ Name = '11'; Binary = 'output/WeaselSetup.exe'; Pdb = 'output/WeaselSetup-Win32.pdb'; Machine = 0x14c }
)

$components = @()
foreach ($pair in $productionPairs) {
  $binaryFull = Join-Path $project $pair.Binary
  $pdbFull = Join-Path $project $pair.Pdb
  foreach ($path in @($binaryFull, $pdbFull)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
      throw "Missing production component: $path"
    }
    if ((Get-Item -LiteralPath $path -Force).Length -le 0) {
      throw "Empty production component: $path"
    }
  }
  $name = "production-$($pair.Name)"
  $result = Invoke-SymbolCheck -Name $name -Binary $binaryFull -Pdb $pdbFull -Dia $diaDll -ExpectSuccess $true -ExpectedMachine ([uint16]$pair.Machine)
  $binaryHash = (Get-FileHash -LiteralPath $binaryFull -Algorithm SHA256).Hash
  $pdbHash = (Get-FileHash -LiteralPath $pdbFull -Algorithm SHA256).Hash
  $components += [pscustomobject]@{
    binary = $pair.Binary
    binarySha256 = $binaryHash
    pdb = $pair.Pdb
    pdbSha256 = $pdbHash
    validation = $result
  }
  Write-Host "PASS $name machine=0x$('{0:X4}' -f [uint16]$pair.Machine)"
}

$sourceCommit = & git -C $project rev-parse HEAD
if ($LASTEXITCODE -ne 0) {
  throw 'git rev-parse HEAD failed'
}
$sourceCommit = ($sourceCommit -join '').Trim()
if ($sourceCommit -notmatch '^[0-9a-fA-F]{40}$') {
  throw "Unexpected git HEAD: $sourceCommit"
}

$report = [ordered]@{
  schemaVersion = 1
  sourceCommit = $sourceCommit
  components = @($components)
}
$reportPath = Join-Path $project 'output/archives/native-symbols.json'
$reportDir = [IO.Path]::GetDirectoryName($reportPath)
if (-not (Test-Path -LiteralPath $reportDir)) {
  New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
}
$reportJson = $report | ConvertTo-Json -Depth 10
[IO.File]::WriteAllText($reportPath, $reportJson + "`n", [Text.UTF8Encoding]::new($false))

$destRoot = Join-Path $project 'output/archives/native-symbols'
$outputRoot = Join-Path $project 'output'
foreach ($pair in $productionPairs) {
  $pdbFull = Join-Path $project $pair.Pdb
  $relative = [IO.Path]::GetRelativePath($outputRoot, $pdbFull).Replace('\', '/')
  $dest = Join-Path $destRoot $relative
  $destDir = [IO.Path]::GetDirectoryName($dest)
  if (-not (Test-Path -LiteralPath $destDir)) {
    New-Item -ItemType Directory -Path $destDir -Force | Out-Null
  }
  Copy-Item -LiteralPath $pdbFull -Destination $dest -Force
  if (-not (Test-Path -LiteralPath $dest -PathType Leaf)) {
    throw "Failed to copy PDB: $dest"
  }
}

Write-Host 'PASS production native symbols validated and PDBs copied'

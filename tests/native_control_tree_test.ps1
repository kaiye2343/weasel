$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$mainScript = Join-Path $PSScriptRoot '../tools/test-native-regressions.ps1'
$helperScript = Join-Path $PSScriptRoot '../tools/New-NativeControlTree.ps1'
. $helperScript

function Assert-Throws {
  param(
    [scriptblock]$Action,
    [string]$Description,
    [string]$ExpectedMessagePattern
  )
  $threw = $false
  try {
    & $Action | Out-Null
  } catch {
    $threw = $true
    if ($_.Exception.Message -notmatch $ExpectedMessagePattern) {
      throw "Expected rejection '$Description' to match '$ExpectedMessagePattern', actual: $($_.Exception.Message)"
    }
  }
  if (-not $threw) {
    throw "Expected rejection: $Description"
  }
}

function Get-Sha256 {
  param([string]$Path)
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash
}

$productionFiles = @(
  (Join-Path $project 'WeaselUI/HorizontalLayout.cpp'),
  (Join-Path $project 'WeaselUI/WeaselPanel.cpp'),
  (Join-Path $project 'WeaselUI/WeaselUI.cpp')
)
$productionHashes = @{}
foreach ($path in $productionFiles) {
  $productionHashes[$path] = Get-Sha256 $path
}

$projectProbe = Join-Path $PSScriptRoot ('__native_control_untracked_' + [guid]::NewGuid().ToString('N') + '.txt')
[IO.File]::WriteAllText($projectProbe, "untracked probe`n", [Text.UTF8Encoding]::new($false))
try {
  $mainOutput = @(& $mainScript -PrepareOnly)
} finally {
  if (Test-Path -LiteralPath $projectProbe) {
    Remove-Item -LiteralPath $projectProbe -Force
  }
}

$controlRoot = $null
foreach ($line in $mainOutput) {
  if ($line -match '^CONTROL_ROOT=(.+)$') {
    $controlRoot = $Matches[1].Trim()
  }
}
if (-not $controlRoot) {
  throw 'PrepareOnly did not report CONTROL_ROOT=<scratch>'
}
if (-not (@($mainOutput) -contains 'PASS')) {
  throw 'PrepareOnly did not report PASS'
}
if (-not (Test-Path -LiteralPath $controlRoot -PathType Container)) {
  throw "PrepareOnly scratch does not exist: $controlRoot"
}
$controlRelative = [IO.Path]::GetRelativePath($project, $controlRoot)
if ($controlRelative -notmatch '^\.tools[\\/]native-baseline-[0-9a-fA-F]{32}$') {
  throw "PrepareOnly scratch is not a native baseline root: $controlRoot"
}

$oldAlgorithms = Join-Path $controlRoot 'old-algorithms'
$oldPlacement = Join-Path $controlRoot 'old-placement'
$archives = @(Get-ChildItem -LiteralPath $controlRoot -Filter 'source-*.zip' -File -Force -ErrorAction Stop)
if ($archives.Count -lt 2) {
  throw "Expected two retained source archives, found $($archives.Count)"
}

$requiredSources = @(
  'WeaselUI/HorizontalLayout.cpp',
  'WeaselUI/WeaselPanel.cpp',
  'WeaselUI/WeaselUI.cpp',
  'WeaselSkinPreview/Preview.cpp',
  'weasel.sln'
)
$forbiddenExeNames = @(
  'WeaselSkinPreview.exe',
  'WeaselUI.exe',
  'WeaselServer.exe',
  'WeaselDeployer.exe',
  'WeaselSetup.exe'
)
$probeName = [IO.Path]::GetFileName($projectProbe)
foreach ($snapshot in @($oldAlgorithms, $oldPlacement)) {
  if (-not (Test-Path -LiteralPath $snapshot -PathType Container)) {
    throw "Snapshot is missing: $snapshot"
  }
  foreach ($relative in $requiredSources) {
    if (-not (Test-Path -LiteralPath (Join-Path $snapshot $relative) -PathType Leaf)) {
      throw "Snapshot is missing production source: $snapshot/$relative"
    }
  }
  if (Test-Path -LiteralPath (Join-Path $snapshot '.git')) {
    throw "Snapshot contains .git: $snapshot"
  }
  if (Test-Path -LiteralPath (Join-Path $snapshot '.tools')) {
    throw "Snapshot contains .tools: $snapshot"
  }
  if (Test-Path -LiteralPath (Join-Path $snapshot 'msbuild')) {
    throw "Snapshot contains build output directory: $snapshot"
  }
  foreach ($dependency in @('weasel.props', 'lib64/rime.lib', 'output/rime.dll', 'control-dependencies.json')) {
    if (Test-Path -LiteralPath (Join-Path $snapshot $dependency)) {
      throw "PrepareOnly copied a build dependency: $snapshot/$dependency"
    }
  }
  $files = @(Get-ChildItem -LiteralPath $snapshot -Recurse -File -Force -ErrorAction Stop)
  $compiledArtifacts = @($files | Where-Object { $_.Extension -in @('.obj', '.pdb') })
  if ($compiledArtifacts.Count -gt 0) {
    throw "Snapshot contains obj/pdb artifacts: $($compiledArtifacts[0].FullName)"
  }
  $forbiddenExes = @($files | Where-Object { $forbiddenExeNames -contains $_.Name })
  if ($forbiddenExes.Count -gt 0) {
    throw "Snapshot contains output executables: $($forbiddenExes[0].FullName)"
  }
  if ((Test-Path -LiteralPath (Join-Path $snapshot $probeName)) -or
      (Test-Path -LiteralPath (Join-Path $snapshot ('tests/' + $probeName)))) {
    throw "Snapshot copied the untracked project probe: $snapshot"
  }
}

# Compare the exact blob content with HEAD. This is equivalent to
# `git show HEAD:tools/SkinFileTrace/README.md` without console-encoding drift.
$headBlob = & git -C $project rev-parse 'HEAD:tools/SkinFileTrace/README.md'
if ($LASTEXITCODE -ne 0) {
  throw 'git rev-parse HEAD:tools/SkinFileTrace/README.md failed'
}
$headBlob = ($headBlob -join '').Trim()
foreach ($snapshot in @($oldAlgorithms, $oldPlacement)) {
  $snapshotReadme = Join-Path $snapshot 'tools/SkinFileTrace/README.md'
  $snapshotBlob = & git -C $project hash-object --no-filters -- $snapshotReadme
  if ($LASTEXITCODE -ne 0) {
    throw "git hash-object failed for snapshot README: $snapshotReadme"
  }
  $snapshotBlob = ($snapshotBlob -join '').Trim()
  if ($snapshotBlob -ne $headBlob) {
    throw "Snapshot README is not the HEAD version: $snapshot"
  }
}

foreach ($relative in @('WeaselUI/HorizontalLayout.cpp', 'WeaselUI/WeaselPanel.cpp')) {
  $expectedSourceBlob = & git -C $project rev-parse "HEAD:$relative"
  if ($LASTEXITCODE -ne 0) {
    throw "git rev-parse HEAD:$relative failed"
  }
  $expectedSourceBlob = ($expectedSourceBlob -join '').Trim()
  foreach ($snapshot in @($oldAlgorithms, $oldPlacement)) {
    $snapshotSource = Join-Path $snapshot $relative
    $actualSourceBlob = & git -C $project hash-object --no-filters -- $snapshotSource
    if ($LASTEXITCODE -ne 0) {
      throw "git hash-object failed for snapshot source: $snapshotSource"
    }
    $actualSourceBlob = ($actualSourceBlob -join '').Trim()
    if ($actualSourceBlob -ne $expectedSourceBlob) {
      throw "PrepareOnly changed historical source: $snapshotSource"
    }
  }
}

$scratchProbe = Join-Path $controlRoot 'untracked-probe.txt'
[IO.File]::WriteAllText($scratchProbe, "scratch untracked probe`n", [Text.UTF8Encoding]::new($false))
foreach ($snapshot in @($oldAlgorithms, $oldPlacement)) {
  if (Test-Path -LiteralPath (Join-Path $snapshot 'untracked-probe.txt')) {
    throw "Scratch probe leaked into snapshot: $snapshot"
  }
}

$modifiedLayout = Join-Path $oldAlgorithms 'WeaselUI/HorizontalLayout.cpp'
$otherLayout = Join-Path $oldPlacement 'WeaselUI/HorizontalLayout.cpp'
$productionLayout = Join-Path $project 'WeaselUI/HorizontalLayout.cpp'
$modifiedBefore = Get-Sha256 $modifiedLayout
$otherBefore = Get-Sha256 $otherLayout
$productionBefore = Get-Sha256 $productionLayout
[IO.File]::AppendAllText($modifiedLayout, "`n// native control tree isolation probe`n",
  [Text.UTF8Encoding]::new($false))
$modifiedAfter = Get-Sha256 $modifiedLayout
$otherAfter = Get-Sha256 $otherLayout
$productionAfter = Get-Sha256 $productionLayout
if ($modifiedBefore -eq $modifiedAfter) {
  throw 'Snapshot modification did not change HorizontalLayout.cpp'
}
if ($otherBefore -ne $otherAfter) {
  throw 'Modifying one snapshot changed the other snapshot'
}
if ($productionBefore -ne $productionAfter) {
  throw 'Modifying a snapshot changed the production HorizontalLayout.cpp'
}

Assert-Throws {
  New-NativeControlTree -ProjectRoot $project -Destination $oldAlgorithms
} 'existing destination' 'already exists'
Assert-Throws {
  New-NativeControlTree -ProjectRoot $project -Destination (Join-Path $project '..\native-control-outside')
} 'destination outside project' 'escapes the project root'
Assert-Throws {
  New-NativeControlTree -ProjectRoot $project -Destination $project
} 'project root' 'must not be the project root'
Assert-Throws {
  New-NativeControlTree -ProjectRoot $project -Destination $controlRoot
} 'baseline root equality' 'strictly inside'
Assert-Throws {
  New-NativeControlTree -ProjectRoot $project -Destination (Join-Path $project '.tools/not-a-native-baseline')
} 'wrong baseline directory' 'strictly inside'

$reparseTarget = Join-Path $controlRoot 'reparse-target'
$reparseLink = Join-Path $controlRoot 'reparse-ancestor'
$reparseReady = $false
try {
  New-Item -ItemType Directory -Path $reparseTarget -Force | Out-Null
  try {
    New-Item -ItemType SymbolicLink -Path $reparseLink -Target $reparseTarget -ErrorAction Stop | Out-Null
    $reparseReady = $true
  } catch {
    try {
      New-Item -ItemType Junction -Path $reparseLink -Target $reparseTarget -ErrorAction Stop | Out-Null
      $reparseReady = $true
    } catch {
      Write-Output 'SKIP: reparse ancestor rejection (symlink/junction creation is not permitted; permissions unchanged)'
    }
  }
  if ($reparseReady) {
    Assert-Throws {
      New-NativeControlTree -ProjectRoot $project -Destination (Join-Path $reparseLink 'child')
    } 'reparse target ancestor' 'ReparsePoint is not allowed'
    if (-not (Test-Path -LiteralPath $reparseLink)) {
      throw 'Reparse test link disappeared after rejection'
    }
  }
} finally {
  if ($reparseReady -and (Test-Path -LiteralPath $reparseLink)) {
    try {
      [IO.Directory]::Delete($reparseLink, $false)
    } catch {
    }
  }
}

foreach ($snapshot in @($oldAlgorithms, $oldPlacement)) {
  if (-not (Test-Path -LiteralPath $snapshot -PathType Container)) {
    throw "Helper removed an existing control tree: $snapshot"
  }
}

foreach ($path in $productionFiles) {
  if ((Get-Sha256 $path) -ne $productionHashes[$path]) {
    throw "Production file changed during the control tree test: $path"
  }
}

Write-Output 'PASS'

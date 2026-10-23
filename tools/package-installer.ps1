$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Get-UserMacroValue {
  param(
    [object]$Group,
    [string]$Name
  )
  $node = @($Group.ChildNodes | Where-Object { $_.LocalName -eq $Name }) | Select-Object -First 1
  if (-not $node) {
    throw "weasel.props is missing $Name"
  }
  $value = $node.InnerText.Trim()
  if ([string]::IsNullOrWhiteSpace($value)) {
    throw "weasel.props contains an empty $Name"
  }
  return $value
}

function Get-ComponentSnapshot {
  param([string]$ProjectRoot)

  $relativePaths = [System.Collections.Generic.List[string]]::new()
  foreach ($relativeDir in @('output', 'output/Win32')) {
    $dir = Join-Path $ProjectRoot $relativeDir
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
      throw "Missing output directory: $relativeDir"
    }
    $files = @(Get-ChildItem -LiteralPath $dir -File -Force -ErrorAction Stop |
      Where-Object { $_.Extension -in @('.exe', '.dll', '.ime', '.pdb') })
    foreach ($file in $files) {
      $relativePaths.Add([IO.Path]::GetRelativePath($ProjectRoot, $file.FullName).Replace('\', '/'))
    }
  }

  $toolsDir = Join-Path $ProjectRoot 'output/archives/tools'
  if (-not (Test-Path -LiteralPath $toolsDir -PathType Container)) {
    throw 'Missing output directory: output/archives/tools'
  }
  foreach ($file in Get-ChildItem -LiteralPath $toolsDir -Recurse -File -Force -ErrorAction Stop) {
    $relativePaths.Add([IO.Path]::GetRelativePath($ProjectRoot, $file.FullName).Replace('\', '/'))
  }

  $components = [System.Collections.Generic.List[object]]::new()
  foreach ($relative in @($relativePaths | Sort-Object)) {
    $full = Join-Path $ProjectRoot $relative
    $hash = (Get-FileHash -LiteralPath $full -Algorithm SHA256 -ErrorAction Stop).Hash
    $components.Add([pscustomobject]@{
      path = $relative
      sha256 = $hash
    })
  }
  return @($components.ToArray())
}

function Assert-SnapshotUnchanged {
  param($Before, $After)
  $beforePaths = @($Before | ForEach-Object { $_.path })
  $afterPaths = @($After | ForEach-Object { $_.path })
  if (($beforePaths -join "`n") -ne ($afterPaths -join "`n")) {
    throw 'Production component file list changed during installer build'
  }
  $afterHashes = @{}
  foreach ($item in $After) {
    $afterHashes[$item.path] = $item.sha256
  }
  foreach ($item in $Before) {
    if (-not $afterHashes.ContainsKey($item.path)) {
      throw "Production component disappeared during installer build: $($item.path)"
    }
    if ($afterHashes[$item.path] -ne $item.sha256) {
      throw "Production component changed during installer build: $($item.path)"
    }
  }
}

$propsPath = Join-Path $project 'weasel.props'
if (-not (Test-Path -LiteralPath $propsPath -PathType Leaf)) {
  throw 'weasel.props is missing'
}
if ((Get-Item -LiteralPath $propsPath -Force).Length -le 0) {
  throw 'weasel.props is empty'
}

$xml = [xml](Get-Content -LiteralPath $propsPath -Raw -ErrorAction Stop)
$groups = @($xml.DocumentElement.ChildNodes | Where-Object {
  $_.LocalName -eq 'PropertyGroup' -and $_.GetAttribute('Label') -eq 'UserMacros'
})
if ($groups.Count -eq 0) {
  throw 'weasel.props has no UserMacros PropertyGroup'
}
$userMacros = $groups[0]

$major = Get-UserMacroValue $userMacros 'VERSION_MAJOR'
$minor = Get-UserMacroValue $userMacros 'VERSION_MINOR'
$patch = Get-UserMacroValue $userMacros 'VERSION_PATCH'
$productVersion = Get-UserMacroValue $userMacros 'PRODUCT_VERSION'
$fileVersion = Get-UserMacroValue $userMacros 'FILE_VERSION'

foreach ($pair in @(@('VERSION_MAJOR', $major), @('VERSION_MINOR', $minor), @('VERSION_PATCH', $patch))) {
  if ($pair[1] -notmatch '^\d+$') {
    throw "weasel.props has an invalid $($pair[0]): $($pair[1])"
  }
}
if ($productVersion -notmatch '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+(?:\.[A-Za-z0-9-]+)?$') {
  throw "weasel.props has an invalid PRODUCT_VERSION: $productVersion"
}
$fileParts = @($fileVersion.Split('.'))
if ($fileParts.Count -ne 4) {
  throw "weasel.props FILE_VERSION must have four numeric segments: $fileVersion"
}
foreach ($part in $fileParts) {
  if ($part -notmatch '^\d+$') {
    throw "weasel.props FILE_VERSION must be numeric: $fileVersion"
  }
}
if ([int]$fileParts[0] -ne [int]$major -or
    [int]$fileParts[1] -ne [int]$minor -or
    [int]$fileParts[2] -ne [int]$patch) {
  throw "weasel.props FILE_VERSION does not match VERSION_MAJOR/MINOR/PATCH: $fileVersion"
}
$version = "$major.$minor.$patch"
$build = $fileParts[3]
if ($productVersion -ne "$version.$build" -and
    -not $productVersion.StartsWith("$version.$build.")) {
  throw 'PRODUCT_VERSION does not match FILE_VERSION'
}

$requiredComponents = @(
  'output/WeaselServer.exe',
  'output/WeaselDeployer.exe',
  'output/WeaselSetup.exe',
  'output/weasel.dll',
  'output/weaselx64.dll',
  'output/weasel.ime',
  'output/weaselx64.ime',
  'output/WeaselSkinPreview.exe',
  'output/rime.dll',
  'output/WinSparkle.dll',
  'output/Win32/WeaselServer.exe',
  'output/Win32/WeaselDeployer.exe',
  'output/Win32/WeaselSkinPreview.exe',
  'output/Win32/rime.dll',
  'output/Win32/WinSparkle.dll',
  'output/archives/tools/Shorekeeper.Manager.exe',
  'output/archives/tools/WeaselSkinPreview.exe',
  'output/archives/tools/Designer/index.html',
  # Fixed files referenced by output/install.nsi; NSIS is read-only here.
  'output/LICENSE.txt',
  'output/README.txt',
  'output/7-zip-license.txt',
  'output/7z.dll',
  'output/7z.exe',
  'output/COPYING-curl.txt',
  'output/curl.exe',
  'output/curl-ca-bundle.crt',
  'output/rime-install.bat',
  'output/rime-install-config.bat',
  'output/start_service.bat',
  'output/stop_service.bat'
)
foreach ($relative in $requiredComponents) {
  $full = Join-Path $project $relative
  if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
    throw "Missing required production component: $relative"
  }
  if ((Get-Item -LiteralPath $full -Force).Length -le 0) {
    throw "Required production component is empty: $relative"
  }
}

$before = Get-ComponentSnapshot $project

$programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
$nsisPath = $null
if ($programFilesX86) {
  $candidate = Join-Path $programFilesX86 'NSIS/Bin/makensis.exe'
  if (Test-Path -LiteralPath $candidate -PathType Leaf) {
    $nsisPath = $candidate
  }
}
if (-not $nsisPath) {
  $command = Get-Command makensis.exe -ErrorAction SilentlyContinue
  if ($command -and $command.Source) {
    $nsisPath = $command.Source
  }
}
if (-not $nsisPath) {
  throw 'NSIS makensis.exe was not found'
}

$nsisArgs = @(
  ('/DWEASEL_VERSION=' + $version),
  ('/DWEASEL_BUILD=' + $build),
  ('/DPRODUCT_VERSION=' + $productVersion),
  '/DRIME_SETTINGS_CENTER_PACKAGE=1',
  (Join-Path $project 'output/install.nsi')
)
Push-Location $project
try {
  & $nsisPath @nsisArgs
  if ($LASTEXITCODE -ne 0) {
    throw "NSIS installer build failed with exit code $LASTEXITCODE"
  }
} finally {
  Pop-Location
}

$installerPath = Join-Path $project ('output/archives/weasel-' + $productVersion + '-installer.exe')
if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
  throw "Installer was not created: $installerPath"
}
if ((Get-Item -LiteralPath $installerPath -Force).Length -le 0) {
  throw "Installer is empty: $installerPath"
}

$after = Get-ComponentSnapshot $project
Assert-SnapshotUnchanged $before $after

$sourceCommit = & git -C $project rev-parse HEAD
if ($LASTEXITCODE -ne 0) {
  throw 'git rev-parse HEAD failed'
}
$sourceCommit = ($sourceCommit -join '').Trim()
if ($sourceCommit -notmatch '^[0-9a-fA-F]{40}$') {
  throw "Unexpected git HEAD value: $sourceCommit"
}

$componentList = @($before | ForEach-Object {
  [ordered]@{
    path = $_.path
    sha256 = $_.sha256
  }
})
$manifest = [ordered]@{
  schemaVersion = 1
  sourceCommit = $sourceCommit
  components = $componentList
}
$jsonPath = Join-Path $project 'output/archives/production-components.json'
$json = $manifest | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText($jsonPath, $json + "`n", [Text.UTF8Encoding]::new($false))

Write-Output "PASS installer built with $($before.Count) unchanged production components"

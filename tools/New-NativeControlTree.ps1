function New-NativeControlTree {
  param(
    [string]$ProjectRoot,
    [string]$Destination,
    [string]$SourceCommit = 'HEAD',
    [switch]$IncludeBuildDependencies
  )

  $ErrorActionPreference = 'Stop'

  function Get-NormalizedFullPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.Length -gt 3) {
      $full = $full.TrimEnd([char[]]@('\', '/'))
    }
    return $full
  }

  function Assert-NoReparsePoint([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
      return
    }
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
      throw "ReparsePoint is not allowed: $Path"
    }
  }

  function Assert-PathAndAncestorsNoReparsePoint([string]$Path, [string]$StopAt) {
    $current = $Path
    while ($true) {
      Assert-NoReparsePoint $current
      if ($current -eq $StopAt) {
        break
      }
      $parent = [IO.Path]::GetDirectoryName($current)
      if ([string]::IsNullOrEmpty($parent) -or $parent -eq $current) {
        break
      }
      $current = $parent
    }
  }

  function Copy-TreeFile([string]$Source, [string]$Target, [string]$Root, $Records) {
    Assert-PathAndAncestorsNoReparsePoint $Source $Root
    $sourceItem = Get-Item -LiteralPath $Source -Force -ErrorAction Stop
    if ($sourceItem.PSIsContainer) {
      throw "Expected a file: $Source"
    }
    $targetDir = [IO.Path]::GetDirectoryName($Target)
    if (-not (Test-Path -LiteralPath $targetDir)) {
      New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    }
    Copy-Item -LiteralPath $Source -Destination $Target -Force
    $relative = [IO.Path]::GetRelativePath($Root, $Source).Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash
    $Records.Add([pscustomobject]@{
      path = $relative
      sha256 = $hash
    })
  }

  function Copy-TreeDirectory([string]$SourceDir, [string]$TargetDir, [string]$Root, $Records) {
    Assert-PathAndAncestorsNoReparsePoint $SourceDir $Root
    $sourceItem = Get-Item -LiteralPath $SourceDir -Force -ErrorAction Stop
    if (-not $sourceItem.PSIsContainer) {
      throw "Expected a directory: $SourceDir"
    }
    if (-not (Test-Path -LiteralPath $TargetDir)) {
      New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
    }
    foreach ($child in Get-ChildItem -LiteralPath $SourceDir -Force -ErrorAction Stop) {
      if (($child.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "ReparsePoint is not allowed: $($child.FullName)"
      }
      $target = Join-Path $TargetDir $child.Name
      if ($child.PSIsContainer) {
        Copy-TreeDirectory $child.FullName $target $Root $Records
      } else {
        Copy-TreeFile $child.FullName $target $Root $Records
      }
    }
  }

  $resolved = Resolve-Path -LiteralPath $ProjectRoot -ErrorAction Stop
  $projectRootFull = Get-NormalizedFullPath $resolved.ProviderPath
  if (-not (Test-Path -LiteralPath (Join-Path $projectRootFull '.git'))) {
    throw "ProjectRoot is not the real project root: $ProjectRoot"
  }
  if (-not (Test-Path -LiteralPath (Join-Path $projectRootFull 'weasel.sln') -PathType Leaf)) {
    throw "ProjectRoot is not the real project root: $ProjectRoot"
  }
  Assert-NoReparsePoint $projectRootFull

  $destinationFull = Get-NormalizedFullPath $Destination
  if ($destinationFull -eq $projectRootFull) {
    throw "Destination must not be the project root: $Destination"
  }
  $relative = [IO.Path]::GetRelativePath($projectRootFull, $destinationFull)
  if ([string]::IsNullOrWhiteSpace($relative) -or $relative -eq '.') {
    throw "Destination must be strictly inside .tools/native-baseline-<guid>/: $Destination"
  }
  $segments = @($relative -split '[\\/]')
  if ($segments -contains '..') {
    throw "Destination escapes the project root: $Destination"
  }
  if ($segments.Count -lt 3 -or
      $segments[0] -ne '.tools' -or
      $segments[1] -notmatch '^native-baseline-[0-9a-fA-F]{32}$') {
    throw "Destination must be strictly inside .tools/native-baseline-<guid>/: $Destination"
  }
  if (Test-Path -LiteralPath $destinationFull) {
    throw "Destination already exists: $destinationFull"
  }

  Assert-PathAndAncestorsNoReparsePoint $destinationFull $projectRootFull

  $baselineRoot = Join-Path $projectRootFull (Join-Path $segments[0] $segments[1])
  if (-not (Test-Path -LiteralPath $baselineRoot)) {
    New-Item -ItemType Directory -Path $baselineRoot -Force | Out-Null
  }
  $destinationParent = [IO.Path]::GetDirectoryName($destinationFull)
  if (-not (Test-Path -LiteralPath $destinationParent)) {
    New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
  }

  $tree = & git -C $projectRootFull ls-tree -r $SourceCommit
  if ($LASTEXITCODE -ne 0) {
    throw "git ls-tree failed for SourceCommit $SourceCommit"
  }
  $symlinkEntries = @($tree | Where-Object { $_ -match '^120000\s' })
  if ($symlinkEntries.Count -gt 0) {
    throw "SourceCommit $SourceCommit contains symlink entries; refusing to archive"
  }

  $archivePath = Join-Path $baselineRoot ("source-{0}.zip" -f [guid]::NewGuid().ToString('N'))
  & git -C $projectRootFull archive --format=zip "--output=$archivePath" $SourceCommit | Out-Null
  if ($LASTEXITCODE -ne 0) {
    throw "git archive failed for SourceCommit $SourceCommit"
  }
  if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    throw "git archive did not create $archivePath"
  }
  if ((Get-Item -LiteralPath $archivePath -Force).Length -le 0) {
    throw "git archive produced an empty archive: $archivePath"
  }

  Expand-Archive -LiteralPath $archivePath -DestinationPath $destinationFull -ErrorAction Stop | Out-Null

  if ($IncludeBuildDependencies) {
    $records = [System.Collections.Generic.List[object]]::new()

    $mandatory = @('weasel.props', 'lib64/rime.lib', 'output/rime.dll')
    foreach ($relativePath in $mandatory) {
      $source = Join-Path $projectRootFull $relativePath
      if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required build dependency is missing: $relativePath"
      }
      $item = Get-Item -LiteralPath $source -Force -ErrorAction Stop
      if ($item.Length -le 0) {
        throw "Required build dependency is empty: $relativePath"
      }
      Assert-PathAndAncestorsNoReparsePoint $source $projectRootFull
    }

    Copy-TreeFile (Join-Path $projectRootFull 'weasel.props') `
      (Join-Path $destinationFull 'weasel.props') $projectRootFull $records
    Copy-TreeFile (Join-Path $projectRootFull 'lib64/rime.lib') `
      (Join-Path $destinationFull 'lib64/rime.lib') $projectRootFull $records
    Copy-TreeFile (Join-Path $projectRootFull 'output/rime.dll') `
      (Join-Path $destinationFull 'output/rime.dll') $projectRootFull $records

    $winSparkle = Join-Path $projectRootFull 'output/WinSparkle.dll'
    if (Test-Path -LiteralPath $winSparkle -PathType Leaf) {
      Copy-TreeFile $winSparkle (Join-Path $destinationFull 'output/WinSparkle.dll') `
        $projectRootFull $records
    }

    $envBat = Join-Path $projectRootFull 'env.bat'
    if (Test-Path -LiteralPath $envBat -PathType Leaf) {
      Copy-TreeFile $envBat (Join-Path $destinationFull 'env.bat') $projectRootFull $records
    }

    $includeDir = Join-Path $projectRootFull 'include'
    $rimeHeaders = @()
    if (Test-Path -LiteralPath $includeDir -PathType Container) {
      Assert-PathAndAncestorsNoReparsePoint $includeDir $projectRootFull
      $rimeHeaders = @(Get-ChildItem -LiteralPath $includeDir -Filter 'rime*.h' -File -Force -ErrorAction Stop |
        Sort-Object Name)
    }
    if ($rimeHeaders.Count -eq 0) {
      throw 'QUESTION: include/rime*.h is missing; the build SDK is incomplete and the whitelist must not be expanded.'
    }
    foreach ($header in $rimeHeaders) {
      Copy-TreeFile $header.FullName (Join-Path (Join-Path $destinationFull 'include') $header.Name) `
        $projectRootFull $records
    }

    $glogDir = Join-Path $projectRootFull 'include/glog'
    if (Test-Path -LiteralPath $glogDir) {
      Assert-PathAndAncestorsNoReparsePoint $glogDir $projectRootFull
      $glogItem = Get-Item -LiteralPath $glogDir -Force -ErrorAction Stop
      if (-not $glogItem.PSIsContainer) {
        throw "include/glog is not a directory: $glogDir"
      }
      Copy-TreeDirectory $glogDir (Join-Path $destinationFull 'include/glog') `
        $projectRootFull $records
    }

    $snapshotRecords = @($records | Sort-Object -Property path)
    $json = ConvertTo-Json -InputObject $snapshotRecords -Depth 4
    [IO.File]::WriteAllText((Join-Path $destinationFull 'control-dependencies.json'), $json + "`n",
      [Text.UTF8Encoding]::new($false))
  }

  return $destinationFull
}

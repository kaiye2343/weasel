param(
  [string]$Baseline = '549eb69',
  [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch = Join-Path $project ('.tools/native-baseline-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
. (Join-Path $PSScriptRoot 'New-NativeControlTree.ps1')

$sources = @('WeaselUI/HorizontalLayout.cpp', 'WeaselUI/WeaselPanel.cpp')

function BuildPreview([string]$ControlRoot) {
  $root = [IO.Path]::GetFullPath($ControlRoot)
  $solutionDir = $root.TrimEnd([char[]]@('\', '/')) + '\'
  $logPath = Join-Path $root 'control-build.log'
  $buildArgs = @(
    '/m',
    '/p:Configuration=Release',
    '/p:Platform=x64',
    "/p:SolutionDir=$solutionDir",
    "/flp:logfile=$logPath;append"
  )
  Push-Location $root
  try {
    & msbuild WeaselUI/WeaselUI.vcxproj @buildArgs /t:Rebuild
    if ($LASTEXITCODE -ne 0) {
      throw "Control UI compilation failed: $root"
    }
    & msbuild WeaselSkinPreview/WeaselSkinPreview.vcxproj @buildArgs
    if ($LASTEXITCODE -ne 0) {
      throw "Control preview compilation failed: $root"
    }
  } finally {
    Pop-Location
  }
}

function ExpectFailure([string]$arguments, [int]$expected, [string]$log, [string]$ControlRoot) {
  $exe = Join-Path $ControlRoot 'output/WeaselSkinPreview.exe'
  $logPath = Join-Path $ControlRoot $log
  $test = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $ControlRoot -PassThru -WindowStyle Hidden
  if (!$test.WaitForExit(60000)) {
    Stop-Process -Id $test.Id
    throw "Control regression timed out: $ControlRoot"
  }
  if (!(Test-Path -LiteralPath $logPath)) {
    throw "Control log missing: $logPath"
  }
  if ($test.ExitCode -ne $expected) {
    throw "Control must reproduce the specific failure ($expected), actual $($test.ExitCode): $ControlRoot"
  }
  if ($expected -eq 6 -and
      (Get-Content -LiteralPath $logPath -Raw) -notmatch 'context-change frames=[1-9][0-9]*') {
    throw "Control frame probe did not observe the control repaint: $logPath"
  }
}

$includeBuildDependencies = -not $PrepareOnly
$oldAlgorithms = New-NativeControlTree -ProjectRoot $project `
  -Destination (Join-Path $scratch 'old-algorithms') `
  -SourceCommit 'HEAD' `
  -IncludeBuildDependencies:$includeBuildDependencies
$oldPlacement = New-NativeControlTree -ProjectRoot $project `
  -Destination (Join-Path $scratch 'old-placement') `
  -SourceCommit 'HEAD' `
  -IncludeBuildDependencies:$includeBuildDependencies

Write-Output "CONTROL_ROOT=$scratch"

if ($PrepareOnly) {
  Write-Output 'PASS'
  return
}

foreach ($relative in $sources) {
  $old = & git -C $project show "${Baseline}:$relative"
  if ($LASTEXITCODE -ne 0) {
    throw "Baseline source missing: $relative"
  }
  $baselineText = ($old -join "`n") + "`n"
  if ($relative -eq 'WeaselUI/WeaselPanel.cpp') {
    # Give the baseline the identical passive frame probe, without changing its redraw decisions.
    # Otherwise missing instrumentation would masquerade as a reproduced rendering failure.
    $call = "  UpdateLayeredWindow(m_hWnd, ScreenDC, &WindowPosAtScreen, &sz, dc,`n                      &PointOriginal, RGB(0, 0, 0), &bf, ULW_ALPHA);"
    if (!$baselineText.Contains($call)) {
      throw 'Baseline layered-window seam changed'
    }
    $probe = @'
  if (UpdateLayeredWindow(m_hWnd, ScreenDC, &WindowPosAtScreen, &sz, dc,
                          &PointOriginal, RGB(0, 0, 0), &bf, ULW_ALPHA)) {
    if (::GetPropW(m_hWnd, L"Weasel.RenderProbe")) {
      auto generation = reinterpret_cast<ULONG_PTR>(::GetPropW(m_hWnd, L"Weasel.RenderGeneration"));
      ::SetPropW(m_hWnd, L"Weasel.RenderGeneration", reinterpret_cast<HANDLE>(generation + 1));
      DIBSECTION section = {};
      if (::GetObjectW(::GetCurrentObject(dc, OBJ_BITMAP), sizeof(section), &section) && section.dsBm.bmBits) {
        auto pixels = static_cast<const BYTE*>(section.dsBm.bmBits);
        auto length = static_cast<size_t>(section.dsBm.bmWidthBytes) * abs(section.dsBm.bmHeight);
        ULONG_PTR hash = 2166136261UL;
        for (size_t i = 0; i < length; ++i) hash = (hash ^ pixels[i]) * 16777619UL;
        ::SetPropW(m_hWnd, L"Weasel.RenderHash", reinterpret_cast<HANDLE>(hash));
      }
    }
  }
'@
    $baselineText = $baselineText.Replace($call, $probe)
    # The historical panel predates the local placement API. Keep this neutral
    # adapter so current UI lifetime code links, without adding the new algorithm.
    $baselineText += "`nvoid WeaselPanel::ReloadPlacement() {}`n"
  }
  [IO.File]::WriteAllText((Join-Path $oldAlgorithms $relative), $baselineText,
    [Text.UTF8Encoding]::new($false))
}

BuildPreview $oldAlgorithms
ExpectFailure '--layout-test' 4 'native-layout-test.log' $oldAlgorithms
ExpectFailure '--manifest skins/shorekeeper/manifest.json --variant watermelon --redraw-test true' 6 'native-redraw-test.log' $oldAlgorithms
Write-Output 'PASS old algorithms reproduce mixed-annotation alignment and unchanged-context image redraw failures'

$uiFile = Join-Path $oldPlacement 'WeaselUI/WeaselUI.cpp'
$currentUi = [IO.File]::ReadAllText($uiFile)
$oldCondition = $currentUi.Replace('if (!shown) panel.ReloadPlacement();', 'if (needsPaint) panel.ReloadPlacement();')
if ($oldCondition -eq $currentUi) {
  throw 'Placement negative-control seam changed'
}
[IO.File]::WriteAllText($uiFile, $oldCondition, [Text.UTF8Encoding]::new($false))

BuildPreview $oldPlacement
ExpectFailure '--placement-test' 8 'native-placement-test.log' $oldPlacement
$failure = Get-Content -LiteralPath (Join-Path $oldPlacement 'native-placement-test.log') -Raw
if ($failure -notmatch 'visible_and_internally_hidden_updates_do_not_read_preferences=0' -or
    $failure -notmatch 'refresh_and_reshow_preserve_caret_anchor=1' -or
    $failure -notmatch 'recreated_window_replays_source_caret_and_preview_path=1') {
  throw 'Placement negative control failed for an unrelated reason'
}
Write-Output 'PASS old Show condition reproduces extra placement reads after owner hides'

foreach ($log in @(
    (Join-Path $oldAlgorithms 'control-build.log'),
    (Join-Path $oldAlgorithms 'native-layout-test.log'),
    (Join-Path $oldAlgorithms 'native-redraw-test.log'),
    (Join-Path $oldPlacement 'control-build.log'),
    (Join-Path $oldPlacement 'native-placement-test.log'))) {
  Write-Output "CONTROL_LOG=$log"
}

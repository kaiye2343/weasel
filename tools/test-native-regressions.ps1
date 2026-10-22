param([string]$Baseline='549eb69')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch=Join-Path $project ('.tools/native-baseline-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$sources=@('WeaselUI/HorizontalLayout.cpp','WeaselUI/WeaselPanel.cpp')
$buildArgs=@('/m','/p:Configuration=Release','/p:Platform=x64',"/p:SolutionDir=$project\")
function BuildPreview {
 & msbuild WeaselUI/WeaselUI.vcxproj @buildArgs /t:Rebuild
 if($LASTEXITCODE -ne 0){throw 'Regression UI compilation failed'}
 & msbuild WeaselSkinPreview/WeaselSkinPreview.vcxproj @buildArgs
 if($LASTEXITCODE -ne 0){throw 'Regression preview compilation failed'}
}
function ExpectFailure([string]$arguments,[int]$expected,[string]$log) {
 $test=Start-Process output/WeaselSkinPreview.exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
 if(!$test.WaitForExit(60000)){Stop-Process -Id $test.Id;throw 'Baseline regression timed out'}
 if(Test-Path -LiteralPath $log){Copy-Item -LiteralPath $log -Destination (Join-Path $scratch $log)}
 if($test.ExitCode -ne $expected){throw "Baseline must reproduce the specific failure ($expected), actual $($test.ExitCode)"}
 if($expected -eq 6 -and (Get-Content -LiteralPath $log -Raw) -notmatch 'context-change frames=[1-9][0-9]*') { throw 'Baseline frame probe did not observe the control repaint' }
}
try {
 foreach($relative in $sources){
  $source=Join-Path $project $relative
  Copy-Item -LiteralPath $source -Destination (Join-Path $scratch ([IO.Path]::GetFileName($source)))
  $old=& git show "${Baseline}:$relative"
  if($LASTEXITCODE -ne 0){throw 'Baseline source missing'}
  $baselineText=($old -join "`n")+"`n"
  if($relative -eq 'WeaselUI/WeaselPanel.cpp') {
   # Give the baseline the identical passive frame probe, without changing its redraw decisions.
   # Otherwise missing instrumentation would masquerade as a reproduced rendering failure.
   $call="  UpdateLayeredWindow(m_hWnd, ScreenDC, &WindowPosAtScreen, &sz, dc,`n                      &PointOriginal, RGB(0, 0, 0), &bf, ULW_ALPHA);"
   if(!$baselineText.Contains($call)){throw 'Baseline layered-window seam changed'}
   $probe=@'
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
   $baselineText=$baselineText.Replace($call,$probe)
   # The historical panel predates the local placement API. Keep this neutral
   # adapter so current UI lifetime code links, without adding the new algorithm.
   $baselineText += "`nvoid WeaselPanel::ReloadPlacement() {}`n"
  }
  [IO.File]::WriteAllText($source,$baselineText,[Text.UTF8Encoding]::new($false))
 }
 BuildPreview
 ExpectFailure '--layout-test' 4 'native-layout-test.log'
 ExpectFailure '--manifest skins/shorekeeper/manifest.json --variant watermelon --redraw-test true' 6 'native-redraw-test.log'
 Write-Output 'PASS baseline reproduces mixed-annotation alignment and unchanged-context image redraw failures'
}finally{
 foreach($relative in $sources){
  $saved=Join-Path $scratch ([IO.Path]::GetFileName($relative))
  if(Test-Path -LiteralPath $saved){Copy-Item -LiteralPath $saved -Destination (Join-Path $project $relative) -Force}
 }
}
BuildPreview
$uiFile = Join-Path $project 'WeaselUI/WeaselUI.cpp'
$currentUi = [IO.File]::ReadAllText($uiFile)
try {
 $oldCondition = $currentUi.Replace('if (!shown) panel.ReloadPlacement();', 'if (needsPaint) panel.ReloadPlacement();')
 if ($oldCondition -eq $currentUi) { throw 'Placement read negative-control seam changed' }
 [IO.File]::WriteAllText($uiFile, $oldCondition, [Text.UTF8Encoding]::new($false))
 BuildPreview
 ExpectFailure '--placement-test' 8 'native-placement-test.log'
 $failure = Get-Content -LiteralPath native-placement-test.log -Raw
 if ($failure -notmatch 'visible_and_internally_hidden_updates_do_not_read_preferences=0' -or
     $failure -notmatch 'refresh_and_reshow_preserve_caret_anchor=1' -or
     $failure -notmatch 'recreated_window_replays_source_caret_and_preview_path=1') { throw 'Placement negative control failed for an unrelated reason' }
 Write-Output 'PASS old Show condition reproduces extra placement reads after owner hides'
} finally {
 [IO.File]::WriteAllText($uiFile, $currentUi, [Text.UTF8Encoding]::new($false))
}
BuildPreview
$fixed = Start-Process output/WeaselSkinPreview.exe -ArgumentList '--placement-test' -WindowStyle Hidden -PassThru
if (!$fixed.WaitForExit(60000)) { $fixed.Kill(); throw 'Fixed placement regression timed out' }
Get-Content native-placement-test.log
if ($fixed.ExitCode -ne 0) { throw 'Fixed placement did not pass after negative control' }

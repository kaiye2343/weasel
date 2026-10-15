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
}
try {
 foreach($relative in $sources){
  $source=Join-Path $project $relative
  Copy-Item -LiteralPath $source -Destination (Join-Path $scratch ([IO.Path]::GetFileName($source)))
  $old=& git show "${Baseline}:$relative"
  if($LASTEXITCODE -ne 0){throw 'Baseline source missing'}
  [IO.File]::WriteAllText($source,($old -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
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

param([string]$Tools='output/archives/tools')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$root=Join-Path $project ('.tools/settings-instance-'+[guid]::NewGuid().ToString('N'))
$exe=[IO.Path]::GetFullPath((Join-Path $Tools 'Shorekeeper.Manager.exe'))
New-Item -ItemType Directory -Path "$root/rime" -Force | Out-Null
Get-ChildItem -LiteralPath "$project/tests/settings-fixture" -File | ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination "$root/rime"}
$arguments="--rime-root `"$root/rime`" --data-root `"$root/skins`" --preview-mode --instance-test"
$first=$null;$second=$null
try {
 $first=Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
 for($i=0;$i -lt 100 -and !(Test-Path -LiteralPath "$root/skins/instance-ready.txt");$i++){Start-Sleep -Milliseconds 100}
 if(!(Test-Path -LiteralPath "$root/skins/instance-ready.txt")){throw 'Settings center did not become ready'}
 $second=Start-Process -FilePath $exe -ArgumentList $arguments -PassThru -WindowStyle Hidden
 if(!$second.WaitForExit(10000) -or $second.ExitCode -ne 0){throw 'Repeated launch failed'}
 if(!$first.WaitForExit(10000) -or $first.ExitCode -ne 0){throw 'Original window was not activated'}
 if((Get-Content -LiteralPath "$root/skins/instance-activated.txt") -ne $first.Id.ToString()){throw 'Activation targeted the wrong process'}
 Write-Output 'PASS repeated launch activates the existing isolated settings center'
}finally{
 foreach($process in @($first,$second)){if($process -and !$process.HasExited){Stop-Process -Id $process.Id}}
}

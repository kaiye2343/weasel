$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$scratch=Join-Path $project ('.tools/upstream-ipc-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$header=Get-Content -LiteralPath (Join-Path $project 'include/PipeChannel.h') -Raw
$utf8=[Text.UTF8Encoding]::new($false)
function Compile([string]$name,[string]$text) {
  $directory=Join-Path $scratch $name
  New-Item -ItemType Directory -Path $directory | Out-Null
  [IO.File]::WriteAllText((Join-Path $directory 'PipeChannel.h'),$text,$utf8)
  $exe=Join-Path $directory 'test.exe'
  & cl /nologo /EHsc /std:c++17 /utf-8 /MT /DUNICODE /D_UNICODE /DBOOST_USE_WINAPI_VERSION=0x0603 "/I$directory" "/I$project/include" "/I$env:BOOST_ROOT" /Y- tests/pipe_channel_test.cpp WeaselIPC/PipeChannel.cpp "/Fo$directory/" "/Fe:$exe" /link "/LIBPATH:$env:BOOST_ROOT/stage/lib"
  if($LASTEXITCODE -ne 0) {throw "IPC fixture compilation failed: $name"}
  return $exe
}
function Check([string]$exe,[string]$mode,[int]$expected) {
  $arguments=if($mode){@($mode)}else{@()}
  $log=Join-Path ([IO.Path]::GetDirectoryName($exe)) 'stdout.log'
  $options=@{FilePath=$exe;PassThru=$true;WindowStyle='Hidden';RedirectStandardOutput=$log;RedirectStandardError=($log+'.error.log')}
  if($arguments.Count){$options.ArgumentList=$arguments}
  $process=Start-Process @options
  if(!$process.WaitForExit(30000)){Stop-Process -Id $process.Id;throw 'Isolated IPC regression timed out'}
  Get-Content -LiteralPath $log
  Get-Content -LiteralPath ($log+'.error.log')
  if($process.ExitCode -ne $expected){throw "IPC fixture $mode expected $expected, actual $($process.ExitCode)"}
}
$badReconnect=$header.Replace('_WritePipe(*_GetPipeHandle(), data_sz, pbuff);','_WritePipe(_GetPipeHandle(), data_sz, pbuff);')
if($badReconnect -eq $header){throw 'Reconnect control seam changed'}
$exe=Compile 'bad-reconnect' $badReconnect | Select-Object -Last 1
Check $exe '--reconnect-only' 3
$leaking=$header.Replace('hpipe_ptr{&_DeletePipeHandle};','hpipe_ptr;')
if($leaking -eq $header){throw 'Thread cleanup control seam changed'}
$exe=Compile 'leaking-handles' $leaking | Select-Object -Last 1
Check $exe '--threads-only' 4
$exe=Compile 'fixed' $header | Select-Object -Last 1
Check $exe '' 0
Write-Output 'PASS upstream controls reproduce reconnect failure/handle leak; adapted IPC passes payload, reconnect, isolation and cleanup'

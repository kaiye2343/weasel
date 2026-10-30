param([Parameter(Mandatory)][string]$EngineDll,
      [ValidateRange(1,5)][int]$Warmup=1,
      [ValidateRange(3,10)][int]$Repeats=5)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$engine=(Resolve-Path -LiteralPath $EngineDll).Path
$output=Join-Path $repo ('.tools/long-input-baseline/'+[guid]::NewGuid().ToString('N'))
Push-Location $repo
try {
 $revision=(& git rev-parse HEAD).Trim()
 if($LASTEXITCODE -ne 0){throw 'Cannot establish source revision'}
 & dotnet run --project tools/LongInputBaseline -c Release -p:NuGetAudit=false -- --engine $engine --output $output --revision $revision --warmup $Warmup --repeats $Repeats
 if($LASTEXITCODE -ne 0){throw ('Baseline failed; preserve partial evidence: '+$output)}
 Write-Output ('Baseline report: '+(Join-Path $output 'report.json'))
} finally {Pop-Location}

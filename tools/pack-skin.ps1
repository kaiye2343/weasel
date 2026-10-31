param([string]$Source = "$PSScriptRoot/../skins/shorekeeper", [string]$Destination = '')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$manifest = Get-Content -LiteralPath "$sourcePath/manifest.json" -Raw | ConvertFrom-Json
if (!$Destination) {
    if ($manifest.id -notmatch '^[a-z0-9][a-z0-9._-]{0,63}$' -or $manifest.id.Contains('..') -or $manifest.version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw 'Invalid package identity' }
    $Destination = Join-Path "$PSScriptRoot/../output/archives" "$($manifest.id)-$($manifest.version).rimeskin"
}
$names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
[void]$names.Add('manifest.json')
[void]$names.Add($manifest.preview)
foreach ($variant in $manifest.variants) {
    foreach ($layer in $variant.layers) { [void]$names.Add($layer.path) }
    if ($variant.style.frame) { [void]$names.Add($variant.style.frame.path) }
}
$destinationPath = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $destinationPath) { throw 'Destination exists' }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destinationPath)) | Out-Null
$zip = [IO.Compression.ZipFile]::Open($destinationPath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($name in $names) {
        $file = [IO.Path]::GetFullPath([IO.Path]::Combine($sourcePath, $name))
        if (!$file.StartsWith($sourcePath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or $name.Contains('\') -or $name.Contains(':')) { throw 'Unsafe package path' }
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file, $name, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $zip.Dispose() }
Write-Output $destinationPath

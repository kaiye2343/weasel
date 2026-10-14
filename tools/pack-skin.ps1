param([string]$Source = "$PSScriptRoot/../skins/shorekeeper", [string]$Destination = "$PSScriptRoot/../output/archives/shorekeeper-1.0.0.rimeskin")
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$manifest = Get-Content -LiteralPath "$sourcePath/manifest.json" -Raw | ConvertFrom-Json
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

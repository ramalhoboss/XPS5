# XPS5's release for GitHub, from what build.ps1 made: build\release\XPS5-<version>\
# with the installer, the uninstaller, send.ps1, the README and the licence,
# their SHA-256 in SHA256SUMS.txt, and the same zipped as XPS5-<version>.zip.
#
# Use: .\tools\release.ps1   (after .\build.ps1 -Wsl Ubuntu-24.04)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root "build"

$line = Select-String -Path (Join-Path $root "src\update.h") -Pattern '#define XPS5_VERSION "([0-9.]+)"'
if (-not $line) { throw "no XPS5_VERSION in src\update.h" }
$version = $line.Matches[0].Groups[1].Value

$install = Join-Path $build "XPS5.elf"
$uninstall = Join-Path $build "XPS5-uninstall.elf"
foreach ($f in @($install, $uninstall)) {
    if (-not (Test-Path $f)) { throw "$f not found: run .\build.ps1 -Wsl Ubuntu-24.04 first" }
}
# the installer has to carry the login window: without it, ordinary users cannot log in
if (-not (Test-Path (Join-Path $build "engine\engine-files.tar.xz"))) {
    throw "the installer has no login engine (build\engine): run .\build.ps1 -Wsl Ubuntu-24.04"
}

$name = "XPS5-$version"
$out = Join-Path $build "release\$name"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item $install, $uninstall, (Join-Path $root "send.ps1"), (Join-Path $root "README.md"), (Join-Path $root "LICENSE") $out

$sums = Get-ChildItem $out -File | Sort-Object Name | ForEach-Object {
    "$((Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLower())  $($_.Name)"
}
Set-Content -Encoding ascii (Join-Path $out "SHA256SUMS.txt") $sums

$zip = Join-Path $build "release\$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $out -DestinationPath $zip
Write-Host "OK: $out"
Get-ChildItem $out -File | ForEach-Object { Write-Host ("  {0,-28} {1,10} bytes" -f $_.Name, $_.Length) }
Write-Host "OK: $zip ($((Get-Item $zip).Length) bytes)"

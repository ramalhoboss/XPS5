# Sends an ELF to the PS5's ELF loader (port 9021, which etaHEN starts) and
# shows what the payload prints. By default it sends XPS5's installer, the one
# next to this script or else build\XPS5.elf.
#
# Use: .\send.ps1 -Ps5 <the PS5's IP> [-Elf <file>] [-Port 9021] [-Seconds 300]
#      .\send.ps1 -Ps5 192.168.1.20 -Elf .\XPS5-uninstall.elf

param(
    [Parameter(Mandatory = $true)][string]$Ps5,
    [int]$Port = 9021,
    [string]$Elf = "",
    [int]$Seconds = 300
)

$ErrorActionPreference = "Stop"

if (-not $Elf) {
    $Elf = Join-Path $PSScriptRoot "XPS5.elf"
    if (-not (Test-Path $Elf)) { $Elf = Join-Path $PSScriptRoot "build\XPS5.elf" }
}
if (-not (Test-Path $Elf)) { throw "$Elf not found" }
$bytes = [IO.File]::ReadAllBytes((Resolve-Path $Elf))

$client = [Net.Sockets.TcpClient]::new($Ps5, $Port)
try {
    $stream = $client.GetStream()
    $stream.Write($bytes, 0, $bytes.Length)
    $client.Client.Shutdown([Net.Sockets.SocketShutdown]::Send)

    # the longest wait between two lines; the payload closing ends it sooner
    $stream.ReadTimeout = $Seconds * 1000
    $reader = [IO.StreamReader]::new($stream)
    try {
        while ($null -ne ($line = $reader.ReadLine())) { Write-Host $line }
    } catch [IO.IOException] { }
} finally {
    $client.Close()
}

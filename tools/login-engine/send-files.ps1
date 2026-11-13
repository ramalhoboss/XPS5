# Sends a payload to the PS5's elfldr and, on the same connection, a file
# for its stdin (elfldr reads only the ELF), then prints what the payload
# writes back. For the login engine's tests: xps5-engine-install.elf with
# engine-files.tar, or xps5-login-probe.elf alone.
#
# Uso: .\send-files.ps1 -Ps5 192.168.1.70 -Elf <payload.elf> [-InputFile <ficheiro>] [-Seconds 60]
param(
    [string]$Ps5 = "192.168.1.70",
    [int]$Port = 9021,
    [Parameter(Mandatory = $true)][string]$Elf,
    [string]$InputFile,
    [int]$Seconds = 60
)
$ErrorActionPreference = "Stop"
$client = New-Object Net.Sockets.TcpClient($Ps5, $Port)
try {
    $stream = $client.GetStream()
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path $Elf).ProviderPath)
    $stream.Write($bytes, 0, $bytes.Length)
    if ($InputFile) {
        $file = [IO.File]::OpenRead((Resolve-Path $InputFile).ProviderPath)
        try { $file.CopyTo($stream, 1MB) } finally { $file.Close() }
    }
    # half-close: elfldr has the ELF, and the payload sees the end of its stdin
    $client.Client.Shutdown([Net.Sockets.SocketShutdown]::Send)
    $stream.ReadTimeout = $Seconds * 1000
    $reader = New-Object IO.StreamReader($stream)
    try {
        while ($null -ne ($line = $reader.ReadLine())) { Write-Host $line }
    } catch [IO.IOException] {
        Write-Host "(nothing more for $Seconds s)"
    }
} finally {
    $client.Close()
}

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Port,
    [Parameter(Mandatory = $true)] [string]$Bundle,
    [string]$RemotePath = "/tmp/youyeetoo-uart-dev.tar.gz",
    [int]$BaudRate = 1500000,
    [switch]$Install,
    [switch]$RequireCamera,
    [int]$FrameSize = 32768,
    [int]$TimeoutMs = 30000,
    [int]$FrameRetries = 3
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-Crc32([byte[]]$Bytes) {
    [uint32]$crc = [uint32]::MaxValue
    foreach ($b in $Bytes) {
        $crc = $crc -bxor [uint32]$b
        for ($i = 0; $i -lt 8; $i++) {
            if (($crc -band 1) -ne 0) { $crc = ($crc -shr 1) -bxor 0xedb88320 } else { $crc = $crc -shr 1 }
        }
    }
    return $crc -bxor 0xffffffff
}

function Wait-FrameAck([System.IO.Ports.SerialPort]$Serial, [uint32]$Sequence, [int]$Timeout) {
    $window = [Collections.Generic.Queue[byte]]::new()
    $deadline = [Environment]::TickCount + $Timeout
    while ([Environment]::TickCount -le $deadline) {
        try { $value = $Serial.ReadByte() } catch [TimeoutException] { continue }
        $window.Enqueue([byte]$value)
        if ($window.Count -gt 8) { [void]$window.Dequeue() }
        if ($window.Count -eq 8) {
            $candidate = $window.ToArray()
            if ([Text.Encoding]::ASCII.GetString($candidate, 0, 4) -eq "C01A" -and
                [BitConverter]::ToUInt32($candidate, 4) -eq $Sequence) {
                return
            }
        }
    }
    throw "ACK timeout for frame $Sequence"
}

function Read-UntilText([System.IO.Ports.SerialPort]$Serial, [string]$Marker, [int]$Timeout) {
    $seen = [Text.StringBuilder]::new()
    $deadline = [Environment]::TickCount + $Timeout
    while ([Environment]::TickCount -le $deadline) {
        try { $value = $Serial.ReadByte() } catch [TimeoutException] { continue }
        if ($value -ge 0) {
            [void]$seen.Append([char]$value)
            if ($seen.ToString().EndsWith($Marker, [StringComparison]::Ordinal)) { return $seen.ToString() }
            if ($seen.Length -gt 8192) { [void]$seen.Remove(0, $seen.Length - 4096) }
        }
    }
    throw "Serial marker timeout: $Marker"
}

if (-not (Test-Path -LiteralPath $Bundle -PathType Leaf)) { throw "Bundle not found: $Bundle" }
$bundleBytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Bundle))
$sha = [Security.Cryptography.SHA256]::Create().ComputeHash($bundleBytes)
$shaHex = ([BitConverter]::ToString($sha)).Replace('-', '').ToLowerInvariant()
$serial = [System.IO.Ports.SerialPort]::new($Port, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.ReadTimeout = $TimeoutMs
$serial.WriteTimeout = $TimeoutMs
$serial.Open()
try {
    $serial.DiscardInBuffer()
    $remoteDir = Split-Path -Path $RemotePath -Parent
    $encodedReceiver = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((Get-Content -Raw "$PSScriptRoot\uart_bundle_receiver.py")))
    $bootstrapCommands = @("mkdir -p '$remoteDir'; : > /tmp/uart_bundle_receiver.b64")
    for ($offset = 0; $offset -lt $encodedReceiver.Length; $offset += 768) {
        $length = [Math]::Min(768, $encodedReceiver.Length - $offset)
        $chunk = $encodedReceiver.Substring($offset, $length)
        $bootstrapCommands += "printf '%s' '$chunk' >> /tmp/uart_bundle_receiver.b64"
    }
    $bootstrapCommands += "base64 -d /tmp/uart_bundle_receiver.b64 > /tmp/uart_bundle_receiver.py; rm -f /tmp/uart_bundle_receiver.b64; chmod 755 /tmp/uart_bundle_receiver.py; python3 /tmp/uart_bundle_receiver.py --output '$RemotePath' --size $($bundleBytes.Length) --sha256 $shaHex"
    foreach ($bootstrap in $bootstrapCommands) {
        [byte[]]$bootstrapBytes = [Text.Encoding]::ASCII.GetBytes("$bootstrap`n")
        $serial.Write($bootstrapBytes, 0, $bootstrapBytes.Length)
    }
    [void](Read-UntilText $serial "C01_READY`n" $TimeoutMs)
    [uint32]$sequence = 0
    for ($offset = 0; $offset -lt $bundleBytes.Length; $offset += $FrameSize) {
        $length = [Math]::Min($FrameSize, $bundleBytes.Length - $offset)
        $payload = New-Object byte[] $length
        [Array]::Copy($bundleBytes, $offset, $payload, 0, $length)
        $crc = Get-Crc32 $payload
        $header = New-Object byte[] 16
        [Array]::Copy([Text.Encoding]::ASCII.GetBytes("C01U"), 0, $header, 0, 4)
        [Array]::Copy([BitConverter]::GetBytes($sequence), 0, $header, 4, 4)
        [Array]::Copy([BitConverter]::GetBytes([uint32]$length), 0, $header, 8, 4)
        [Array]::Copy([BitConverter]::GetBytes([uint32]$crc), 0, $header, 12, 4)
        $acked = $false
        for ($attempt = 1; $attempt -le $FrameRetries; $attempt++) {
            $serial.Write($header, 0, $header.Length)
            $serial.Write($payload, 0, $payload.Length)
            try {
                Wait-FrameAck $serial $sequence $TimeoutMs
                $acked = $true
                break
            } catch {
                if ($attempt -eq $FrameRetries) { throw }
            }
        }
        if (-not $acked) { throw "Frame $sequence was not acknowledged" }
        $sequence++
    }
    $end = New-Object byte[] 16
    [Array]::Copy([Text.Encoding]::ASCII.GetBytes("C01U"), 0, $end, 0, 4)
    [Array]::Copy([BitConverter]::GetBytes($sequence), 0, $end, 4, 4)
    $serial.Write($end, 0, $end.Length)
    Wait-FrameAck $serial $sequence $TimeoutMs
    [void](Read-UntilText $serial "UART_RECEIVE_OK" $TimeoutMs)
    Write-Host "UART_UPLOAD_OK path=$RemotePath size=$($bundleBytes.Length) sha256=$shaHex"
    if ($Install) {
        $cameraPrefix = if ($RequireCamera) { 'REQUIRE_CAMERA=1 ' } else { '' }
        $command = "sudo env ${cameraPrefix}bash -c 'd=`$(mktemp -d); tar -xzf `"$RemotePath`" -C `"`$d`"; `"`$d/tools/install_uart_dev_bundle.sh`" `"$RemotePath`"; s=`$?; rm -rf `"`$d`"; exit `$s'"
        $commandBytes = [Text.Encoding]::ASCII.GetBytes("$command`n")
        $serial.Write($commandBytes, 0, $commandBytes.Length)
        Write-Host "UART_INSTALL_COMMAND_SENT"
    }
} catch {
    if ($serial.IsOpen) {
        $abort = New-Object byte[] 16
        [Array]::Copy([Text.Encoding]::ASCII.GetBytes("C01X"), 0, $abort, 0, 4)
        try { $serial.Write($abort, 0, $abort.Length) } catch {}
    }
    throw
} finally {
    $serial.Close()
    $serial.Dispose()
}

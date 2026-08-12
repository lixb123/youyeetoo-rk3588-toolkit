param(
    [Parameter(Mandatory = $true)]
    [string]$ImagePath
)

$resolved = (Resolve-Path -LiteralPath $ImagePath -ErrorAction Stop).Path
$item = Get-Item -LiteralPath $resolved -ErrorAction Stop
if (-not $item.PSIsContainer -and $item.Length -gt 0) {
    $hash = Get-FileHash -LiteralPath $resolved -Algorithm SHA256
    [pscustomobject]@{
        path = $resolved
        size_bytes = $item.Length
        last_write_time = $item.LastWriteTime.ToString('o')
        sha256 = $hash.Hash.ToLowerInvariant()
    } | ConvertTo-Json -Compress
    exit 0
}

throw "Artifact is missing, a directory, or empty: $resolved"

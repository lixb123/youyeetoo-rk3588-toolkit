[CmdletBinding()]
param(
    [string]$PythonPath = 'python'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$python = Get-Command $PythonPath -ErrorAction Stop

Write-Host '[1/4] Ethernet protocol unit tests'
    & $python.Source -m unittest discover `
    -s (Join-Path $repoRoot 'tools/eth-sim/tests') `
    -p 'test_*.py' `
    -v
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host '[2/4] UART protocol unit tests'
& $python.Source (Join-Path $repoRoot 'tools/rk-eth-uart/test_protocol.py')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host '[3/4] Python syntax checks'
$pythonFiles = Get-ChildItem -LiteralPath $repoRoot -Recurse -File -Filter '*.py' |
    Where-Object { $_.FullName -notmatch '[\\/]__pycache__[\\/]' }
foreach ($file in $pythonFiles) {
    & $python.Source -m py_compile $file.FullName
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host '[4/4] Repository hygiene checks'
$forbiddenFiles = Get-ChildItem -LiteralPath $repoRoot -Recurse -File | Where-Object {
    $_.Extension -in '.so', '.img', '.vhdx', '.pem', '.key' -or
    $_.Name -match '^id_(rsa|ecdsa|ed25519)($|\.)'
}
if ($forbiddenFiles) {
    $forbiddenFiles.FullName | ForEach-Object { Write-Error "Forbidden artifact: $_" }
    exit 1
}

Write-Host 'All local checks passed.'

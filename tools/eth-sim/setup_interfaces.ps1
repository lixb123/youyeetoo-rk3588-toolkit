param(
    [string]$PayloadAdapter = '以太网 3',
    [string]$XAdapter = '以太网',
    [switch]$Clear
)

$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '请在管理员 PowerShell 中运行此脚本。'
}

function Remove-SimulationAddresses([string]$Adapter) {
    Get-NetRoute -InterfaceAlias $Adapter -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.DestinationPrefix -match '^(10\.240\.1\.(0|35|36|37|38|39|40|50|51|52)|10\.2\.0\.0)/' } |
        Remove-NetRoute -Confirm:$false -ErrorAction SilentlyContinue
    Get-NetIPAddress -InterfaceAlias $Adapter -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -like '10.240.1.*' -or $_.IPAddress -like '10.2.*' } |
        Remove-NetIPAddress -Confirm:$false -ErrorAction SilentlyContinue
}

Remove-SimulationAddresses $PayloadAdapter
Remove-SimulationAddresses $XAdapter
if ($Clear) {
    Write-Host '已清理 eth-sim 地址和路由。'
    exit 0
}

# 电脑网口0模拟 NB-IoT 载荷，网口1模拟 X 测控机。
New-NetIPAddress -InterfaceAlias $PayloadAdapter -IPAddress 10.240.1.38 -PrefixLength 24 -ErrorAction Stop | Out-Null
foreach ($ip in 35, 37, 39) {
    New-NetIPAddress -InterfaceAlias $PayloadAdapter -IPAddress "10.240.1.$ip" -PrefixLength 24 -SkipAsSource $true -ErrorAction Stop | Out-Null
}
New-NetIPAddress -InterfaceAlias $XAdapter -IPAddress 10.240.1.2 -PrefixLength 30 -ErrorAction Stop | Out-Null

# 精确路由让 X 测控指令从网口1进入板端，即使网口0也使用 10.240.1/24。
New-NetRoute -InterfaceAlias $XAdapter -DestinationPrefix '10.240.1.36/32' -NextHop 10.240.1.1 -PolicyStore ActiveStore -ErrorAction Stop | Out-Null
# 载荷业务和管理/日志目标经开发板载荷口转发。
foreach ($ip in 40, 50, 51, 52) {
    New-NetRoute -InterfaceAlias $PayloadAdapter -DestinationPrefix "10.240.1.$ip/32" -NextHop 10.240.1.34 -PolicyStore ActiveStore -ErrorAction Stop | Out-Null
}
New-NetRoute -InterfaceAlias $PayloadAdapter -DestinationPrefix '10.2.0.0/16' -NextHop 10.240.1.34 -PolicyStore ActiveStore -ErrorAction Stop | Out-Null

Write-Host "eth-sim 已配置：网口0 $PayloadAdapter = 10.240.1.35/.37/.38/.39，网口1 $XAdapter = 10.240.1.2/30"
Write-Host 'X 测控目标 10.240.1.36/32 固定经 10.240.1.1，载荷业务目标经 10.240.1.34。'

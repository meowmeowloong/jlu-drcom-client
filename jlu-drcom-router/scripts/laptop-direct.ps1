# laptop-direct.ps1 —— 笔记本直连宿舍墙口时，接管/归还路由器的校园网身份
#
# 背景：宿舍有线网是静态分配（绑定 MAC + 固定 IP + 网关），没有 DHCP。
# 路由器的 WAN 就配的是这套身份；路由器断电（宿舍晚间断电）时，笔记本可以临时
# 接管同一套身份直连墙口，用 QT 客户端认证上网。
#
# 铁律：takeover 期间路由器必须断电/拔线；路由器上电前必须先 restore。
#       （同一个 MAC + 同一个 IP 同时在线 = 冲突，两条线路一起断）
#
# 用法（管理员 PowerShell）：
#   powershell -ExecutionPolicy Bypass -File laptop-direct.ps1 takeover
#   powershell -ExecutionPolicy Bypass -File laptop-direct.ps1 restore
#
# 注意：本文件必须保存为 UTF-8 with BOM，否则 PowerShell 5.1 会把中文注释按 GBK
# 解析导致脚本直接报错退出（2026-10-04 实测踩坑）。
# 以下参数为本机实测值（2026-10-04）。换电脑/换端口/学校改分配时需要对应修改。

param([Parameter(Mandatory=$true)][ValidateSet('takeover','restore')][string]$mode)

#Requires -RunAsAdministrator

$ErrorActionPreference = 'Continue'
Start-Transcript -Path "$env:TEMP\laptop-direct-last-run.log" -Force

$idx     = 3                      # 有线网卡（Realtek PCIe GbE），用 Get-NetAdapter 确认
$wlanIdx = 4                      # WLAN 网卡（作备用线路）
$mac     = '11-22-33-44-55-66'    # 校园网绑定的 MAC（路由器 WAN 克隆的就是它）
$origMac = '40-C2-BA-44-FD-F3'    # 笔记本有线网卡原 MAC
$ip      = '10.9.8.7'             # 学校分配的静态 IP（/24）
$gw      = '10.9.8.254'           # 网关
$authSrv = '10.100.61.3'          # DrCOM 认证服务器
$dnsList = @('223.5.5.5','119.29.29.29')

if ($mode -eq 'takeover') {

    Write-Host '[前提] 路由器必须已断电或从墙口拔掉！' -ForegroundColor Yellow

    $cur = (Get-NetAdapter -InterfaceIndex $idx).MacAddress
    if ($cur -ne $mac) {
        Write-Host "克隆 MAC: $cur -> $mac"
        Get-NetAdapter -InterfaceIndex $idx | Set-NetAdapter -MacAddress $mac -Confirm:$false
        Start-Sleep -Seconds 8
    } else {
        Write-Host "MAC 已经是 $mac，跳过"
    }

    Write-Host "配置静态 IP: $ip/24 网关 $gw"
    Set-NetIPInterface -InterfaceIndex $idx -Dhcp Disabled -ErrorAction SilentlyContinue
    New-NetIPAddress -InterfaceIndex $idx -IPAddress $ip -PrefixLength 24 -DefaultGateway $gw `
        -ErrorAction SilentlyContinue
    Set-DnsClientServerAddress -InterfaceIndex $idx -ServerAddresses $dnsList

    Write-Host "加认证服务器主机路由 $authSrv/32 -> 有线口（防多网卡时走错出口）"
    Remove-NetRoute -DestinationPrefix "$authSrv/32" -Confirm:$false -ErrorAction SilentlyContinue
    New-NetRoute -DestinationPrefix "$authSrv/32" -InterfaceIndex $idx -NextHop $gw `
        -RouteMetric 1 -ErrorAction SilentlyContinue

    Write-Host 'WLAN 降为备用线路（metric 5000）'
    Set-NetIPInterface -InterfaceIndex $wlanIdx -InterfaceMetric 5000 -ErrorAction SilentlyContinue

    Write-Host "`n完成。现在启动 QT 客户端（DrCOM_JLU_Qt.exe）登录即可上网。" -ForegroundColor Green

} else {

    Write-Host '退出 QT 客户端...'
    taskkill /IM DrCOM_JLU_Qt.exe /F 2>$null

    Write-Host "移除 $authSrv/32 主机路由"
    Remove-NetRoute -DestinationPrefix "$authSrv/32" -Confirm:$false -ErrorAction SilentlyContinue

    Write-Host "恢复原 MAC: $origMac"
    Get-NetAdapter -InterfaceIndex $idx | Set-NetAdapter -MacAddress $origMac -Confirm:$false
    Start-Sleep -Seconds 6

    Write-Host '有线口恢复 DHCP'
    Remove-NetIPAddress -InterfaceIndex $idx -IPAddress $ip -Confirm:$false -ErrorAction SilentlyContinue
    Set-NetIPInterface -InterfaceIndex $idx -Dhcp Enabled -ErrorAction SilentlyContinue
    Set-DnsClientServerAddress -InterfaceIndex $idx -ResetServerAddresses

    Write-Host 'WLAN 恢复自动 metric'
    Set-NetIPInterface -InterfaceIndex $wlanIdx -AutomaticMetric Enabled -ErrorAction SilentlyContinue

    Get-NetAdapter -InterfaceIndex $idx | Restart-NetAdapter
    Start-Sleep -Seconds 8

    Write-Host "`n已还原。现在可以给路由器上电了。" -ForegroundColor Green
}

Stop-Transcript

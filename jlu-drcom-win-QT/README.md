# DrCOM JLU Windows 客户端（Qt）

吉林大学校园网（DrCOM）Windows 桌面客户端，基于 Qt6。
v1.0.0.7 起内置**直连模式**：笔记本网线直连宿舍墙口时，客户端自动接管校园网身份并认证，
退出时自动还原，无需任何外部脚本。

## 功能

- DrCOM JLU 协议完整实现：`challenge → login → keepalive_1 → keepalive_2` 循环保活
- 掉线自动重启重登（可配置次数），托盘常驻
- 记住密码（DPAPI 加密存储）、自动登录、隐藏窗口
- **直连模式（v1.0.0.7 新增）**：见下文

## 直连模式

**适用场景**：宿舍有线网是静态分配（学校绑定 MAC + 分配固定 IP/网关，无 DHCP），
这套身份平时配在路由器 WAN 口上。路由器断电（宿舍晚间断电）时，笔记本可以用
客户端临时接管这套身份直连墙口上网。

**使用步骤**：

1. 登录界面勾选 **「直连模式（接管校园网身份）」**（只需勾一次）；
2. 重启客户端 —— 程序带管理员清单，会自动以管理员运行；
3. 登录前客户端自动完成：
   - 把有线网卡 MAC 克隆成校园网绑定的 MAC；
   - 配静态 IP / 掩码 /24 / 网关 / DNS；
   - 添加认证服务器 `10.100.61.3/32` 主机路由（**关键**：多网卡时防止认证包从错误的
     网卡发出，表现为 challenge 无响应）；
   - 把 WLAN 降为备用线路（metric 5000）；
4. 退出客户端（托盘右键 → Quit）自动还原全部网络配置；
5. 客户端崩溃/强杀留下的残留下次启动自动清理。

**参数配置**：直连参数因人而异，源码里只有占位符。在注册表
`HKCU\Software\DrCOM_JLU_Qt\OrganizationDefaults\direct`（子键）下设置：

| 值名 | 含义 | 示例（PowerShell） |
| --- | --- | --- |
| `mac` | 校园网绑定的 MAC | `11-22-33-44-55-66` |
| `origMac` | 本机有线网卡原 MAC（还原用，留空跳过还原） | `AA-BB-CC-DD-EE-FF` |
| `ip` | 学校分配的静态 IP | `10.9.8.7` |
| `gw` | 网关 | `10.9.8.254` |
| `dns1` / `dns2` | DNS | `223.5.5.5` / `119.29.29.29` |
| `wiredIdx` | 有线网卡 ifIndex（`Get-NetAdapter` 查看） | `3` |
| `wlanIdx` | 无线网卡 ifIndex（降为备用） | `4` |

PowerShell 示例：

```powershell
$p = 'HKCU:\Software\DrCOM_JLU_Qt\OrganizationDefaults\direct'
New-Item -Path $p -Force | Out-Null
Set-ItemProperty -Path $p -Name mac      -Value '11-22-33-44-55-66'
Set-ItemProperty -Path $p -Name origMac  -Value 'AA-BB-CC-DD-EE-FF'
Set-ItemProperty -Path $p -Name ip       -Value '10.9.8.7'
Set-ItemProperty -Path $p -Name gw       -Value '10.9.8.254'
```

**铁律：直连模式与路由器不能同时在线**——同一个 MAC + 同一个 IP，冲突后两条线路一起断。
路由器上电前先退出客户端。

## 编译

环境：Qt 6.8.3 (MinGW) + MinGW 13.1。没有 Qt 可用 aqtinstall 免注册安装
（清华镜像的包可能损坏，推荐阿里云镜像）：

```bash
pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_mingw -O C:/Qt -b https://mirrors.aliyun.com/qt/
python -m aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 -O C:/Qt -b https://mirrors.aliyun.com/qt/

export PATH="/c/Qt/6.8.3/mingw_64/bin:/c/Qt/Tools/mingw1310_64/bin:$PATH"
mkdir build && cd build
qmake ../src/DrCOM_JLU_Qt.pro
mingw32-make -j8 release
windeployqt --release release/DrCOM_JLU_Qt.exe
```

## 已知问题与修复记录

| 问题 | 原因 / 解法 |
| --- | --- |
| `bind failed. Error code: 10013` | UDP 61440 被 Hyper-V/WSL 动态保留。管理员执行 `netsh int ipv4 add excludedportrange protocol=udp startport=61440 numberofports=1 store=persistent` 永久占住 |
| 直连模式下 challenge 无响应 | ① 认证包走了别的网卡（确认 `direct/*` 参数与 `/32` 路由）；② 网线没插墙口；③ 与路由器同时在线冲突 |
| 启动后无日志无反应 | 有一个旧实例占着单实例主位（托盘里找找），或程序目录无写权限 |

协议细节见仓库根目录 [jlu-drcom-protocol.md](../jlu-drcom-protocol.md)。

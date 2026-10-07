# DrCOM JLU Windows 客户端（Qt）

吉林大学校园网（DrCOM）Windows 桌面客户端，基于 Qt6。
v1.0.0.7 起内置**直连模式**：笔记本网线直连宿舍墙口时，客户端自动接管校园网身份并认证，
退出时自动还原，无需任何外部脚本。

## 让 AI 手把手带你配（不想看文档？）

把下面整段复制给任意 AI 助手（豆包 / ChatGPT / Kimi…），它会一步步带你完成：

```
我在吉林大学宿舍，网线直连笔记本（没有路由器，或路由器断电了）。学校有线网是静态分配，
我已在校园网办理了"绑定 MAC + 固定 IP"，拿到了这些信息，但不想在 Windows 网卡属性里手动配置。
请带我使用这个开源客户端完成认证：
https://github.com/zf-666888/jlu-drcom-client （jlu-drcom-win-QT 目录，Releases 页有打包好的 exe）

要求：
1. 指导我下载解压，并以管理员身份运行（程序要改网卡配置，需要管理员）
2. 教我用 PowerShell 的 Get-NetAdapter 找到有线网卡和无线网卡的 ifIndex（接口编号）
3. 在登录窗口"直连模式"分组里填参数：上方 MAC 栏选 custom 填绑定的 MAC；
   IP/掩码位/网关/DNS 按学校分配的填；两个网卡号填刚查到的编号；
   勾选"登录时自动配置网卡并接管身份"
4. 点登录，等窗口下方显示出 IP 即成功；告诉我退出客户端时网卡配置会自动还原
5. 如果失败，让我打开 exe 目录 logs 下最新的 .lgt 文件，把含 [DirectMode] 和 Critical
   的行发给你分析原因
注意：账号密码提醒我不要发到公开聊天记录里。
```

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

**参数配置**：全部在登录窗口的「直连模式」分组里直接填写，客户端会记住：

| 界面字段 | 填什么 |
| --- | --- |
| MAC（上方 MAC 栏） | 选 **custom**，填学校绑定的 MAC —— 认证报文和网卡克隆都用它 |
| IP | 学校分配的静态 IP |
| 掩码位 | 一般是 24（即 255.255.255.0） |
| 网关 | 分配的网关 |
| DNS1 / DNS2 | 可选，不填用系统默认 |
| 网卡号（左） | 有线网卡 ifIndex，PowerShell `Get-NetAdapter` 查看 |
| 网卡号（右） | 备用 WLAN 网卡 ifIndex（登录时自动降为备用线路） |

> 参数实际存储在注册表 `HKCU\Software\DrCOM_JLU_Qt\OrganizationDefaults\direct` 子键下，
> 一般不用手动碰。原来"绑定 MAC 的原 MAC"也无需填写——首次接管时会自动记忆，用于退出还原。

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
| ~~退出卡六七秒、界面托盘无响应~~ 已修复（v1.0.0.9） | 旧版退出时在主线程串行跑 7 个 PowerShell + 死等 6 秒做网络还原。现在还原合并为**单个** PowerShell 后台进程（窗口先隐藏，实测 1 秒内退出），还原输出在 `%TEMP%\drcom-direct-restore.log`；登录时的网卡接管同理挪到后台线程，等网卡从死等 12 秒改为轮询 |

协议细节见仓库根目录 [jlu-drcom-protocol.md](../jlu-drcom-protocol.md)。

# DrCOM Client for Jilin University（吉大校园网客户端合集）

吉大校园网（DrCOM）认证相关项目合集：覆盖 Windows 桌面、路由器、Python、OpenWRT，
并包含「路由器免认证上网」方案 —— 让路由器替你完成认证与保活，手机、电脑连 WiFi
直接上网，不再各自认证。

## 子项目一览

| 目录 / 文件 | 说明 |
| --- | --- |
| `jlu-drcom-router/` | **红米路由器客户端**（不刷机、原厂固件塞静态二进制；含笔记本直连方案） |
| `jlu-drcom-win-QT/` | **Windows 桌面客户端**（Qt6；v1.0.0.7 起内置「直连模式」） |
| `jlu-drcom-py/` | Python 参考实现（`newclient.py` / `client.py`） |
| `jlu-drcom-for_openWRT/` | OpenWRT 版本（legacy） |
| `jlu-drcom-luci/` | OpenWRT LuCI 界面插件 |
| `jlu-drcom-protocol.md` | DrCOM 协议文档 |
| `jlu-drcom-router/操作指南与原理解析报告.md` | 路由器方案的操作指南与原理（含"不限设备数量"的 NAT 原理） |

## 路由器方案（免认证上网）

一句话：路由器开 SSH → 塞入静态编译的 DrCOM 客户端 → 开机自启 + crontab 守护自动拉起，
路由器成为校园网眼里"唯一登录的设备"，全屋设备 NAT 共享这一个会话。

- 详细步骤与验证：[jlu-drcom-router/README.md](jlu-drcom-router/README.md)
- 原理与操作指南：[jlu-drcom-router/操作指南与原理解析报告.md](jlu-drcom-router/操作指南与原理解析报告.md)
- 开启 SSH 的第三方工具 XMiR-Patcher 不在仓库内，按路由器 README 中的链接下载

## Windows 客户端直连模式（v1.0.0.7）

宿舍有线网通常是**静态分配**（绑定 MAC + 固定 IP + 网关，无 DHCP），且身份与路由器
共用。Windows 客户端 v1.0.0.7 起内置「直连模式」：

- 登录界面勾选 **「直连模式（接管校园网身份）」** → 重启客户端（需要管理员权限）→ 登录；
- 客户端自动完成：克隆绑定 MAC、配静态 IP/网关/DNS、加认证服务器 /32 路由、WLAN 降为备用；
- 退出客户端自动还原网络配置；崩溃残留下次启动自动清理；
- 校园网参数（MAC/IP/网关等）在注册表 `HKCU\Software\DrCOM_JLU_Qt\OrganizationDefaults\direct` 下配置，
  键与默认占位符见 [jlu-drcom-win-QT/README.md](jlu-drcom-win-QT/README.md)。

**铁律：直连模式与路由器不能同时在线**（同一个 MAC + 同一个 IP，冲突后两条线路一起断）。

## 开发与排障记录（2026-09）

### 需求背景

在前辈们与本仓库既有客户端的基础上，目标：得到一个可装在**红米路由器**（AX3000 / RA81）
上的插件，免除手机、电脑的频繁认证。硬约束：**不刷第三方固件**，保留原厂 MiWiFi，
仅通过社区工具开 SSH 后塞入静态编译的认证客户端。

### 实施路线

```
XMiR-Patcher 免拆机开 SSH
  → musl 交叉编译静态二进制（armv7 软浮点 ABI，-static -no-pie）
  → scp 上传路由器
  → install.sh 安装到 /data/drcom/ + crontab 每分钟守护
  → 日志出现「登录成功」即完成
```

### 断网故障复盘（09-16 ～ 09-17）

**现象**：路由器没动过，网络突然断了，需要重新认证。

**排查过程**：

1. 当时电脑在用手机热点上网 —— 热点与路由器内网（`192.168.31.x`）是两个互相隔离的网络，
   无法直接访问路由器。解法：手机改开 **USB 网络共享**给电脑提供外网，电脑 WiFi 同时连
   路由器内网，双路并行后 SSH 进去查日志。
2. 日志根因明确：

   ```
   [ERROR] 无法打开配置文件 /etc/drcom.conf: No such file or directory
   ```

   该错误从 09-16 16:50 起**每分钟一条** —— crontab 守护在反复尝试拉起客户端，
   但每次都因找不到配置而失败。
3. **根因**：路由器重启过，重启后**原厂固件清空了 `/etc`**；旧版 `install.sh` 把配置装在
   `/etc/drcom.conf`。二进制和日志在 `/data` 分区（重启不丢），唯独配置丢了。
4. 交叉验证：关掉手机热点、改回网线直连后网络恢复 —— 说明会话失效后客户端没在跑、
   无人重新认证。

**修复**：`install.sh` 改为把配置装到 `/data/drcom/drcom.conf`（持久分区，重启不丢），
服务脚本与 crontab 同步指向新路径；并优化为"路由器上已有填好账号的配置则保留"。

**同类故障快速判断**（按日志区分）：

| 现象 | 怀疑方向 |
| --- | --- |
| 日志停更、进程不在 | 路由器重启后自启/守护未拉起 |
| 一直 `challenge 超时` / 重登循环 | 学校强制重新认证，或网络链路问题 |
| 日志显示被踢下线 | 同一账号在别处登录（如电脑客户端未退） |

> 注意：**电脑上还开着旧 DrCOM 客户端**是最常见的"掉线"来源 —— 它会和路由器抢同一个
> 会话，互相踢，表现为反复"登录成功 → 超时 → 重登"。用路由器方案时务必退掉电脑客户端。

## 配置说明（newclient.py）

```
server = "10.100.61.3" #认证服务器，可以使用域名auth.jlu.edu.cn
username = "" #用户名，和客户端一样
password = "" #密码，和客户端一样
host_name = "JLU-DrCOM" #计算机名，不要超过71个字符
host_os = "Windows 4.0" #操作系统，不要超过128个字符
mac = 0x888888888888 #网络中心上注册时IP对应的MAC
```

## 下载

预编译产物见 [Releases](../../releases)：

| 资产 | 说明 |
| --- | --- |
| `DrCOM_JLU_Qt_v1.0.0.7_direct.zip` | Windows 客户端（含 Qt 运行库，解压即用；直连参数需自行配置） |
| `jlu-drcom-armv7` / `jlu-drcom-aarch64` | 路由器静态二进制（scp 到路由器后 `chmod +x`） |

## 免责声明

本项目仅供学习交流，请遵守所在学校/单位网络使用规定。使用本仓库任何工具造成的
后果由使用者自行承担。

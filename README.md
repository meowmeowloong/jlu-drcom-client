# DrCOM Client for Jilin University（吉大校园网客户端合集）

吉大校园网（DrCOM）认证相关项目合集：覆盖 Windows 桌面、路由器、Python、OpenWRT，
亮点：
包含「路由器免认证上网」方案 —— 让路由器替你完成认证与保活（仍需要账号和密码等信息，初始化一次，永远无感运行！断电、复电不影响！）
手机、电脑连 WiFi
直接上网，不再认证。

## 子项目一览

| 目录 / 文件 | 说明 |
| --- | --- |
| ★ `jlu-drcom-router/` | **红米路由器客户端**（不刷机、原厂固件塞静态二进制 |
| ★ `jlu-drcom-win-QT/` | **Windows 桌面客户端**（Qt6；v1.0.0.8 起内置「直连模式」，断电后笔记本直连，不用路由器的读者也推荐日常使用这个客户端，参数直接在窗口里填更方便） |
| `jlu-drcom-py/` | Python 参考实现（`newclient.py` / `client.py`） |
| `jlu-drcom-for_openWRT/` | OpenWRT 版本（legacy） |
| `jlu-drcom-luci/` | OpenWRT LuCI 界面插件 |
| `jlu-drcom-protocol.md` | DrCOM 协议文档 |
| `jlu-drcom-router/操作指南与原理解析报告.md` | 路由器方案的操作指南与原理（含"不限设备数量"的 NAT 原理） |

## 路由器方案（免认证上网）与 AI 帮装提示词prompt

一句话：路由器开 SSH → 塞入静态编译的 DrCOM 客户端 → 开机自启 + crontab 守护自动拉起，
路由器成为校园网眼里"唯一登录的设备"，全屋设备 NAT 共享这一个会话。

- 提示词、详细步骤与验证：[jlu-drcom-router/README.md](jlu-drcom-router/README.md)
- 原理与操作指南：[jlu-drcom-router/操作指南与原理解析报告.md](jlu-drcom-router/操作指南与原理解析报告.md)
- 开启 SSH 的第三方工具 XMiR-Patcher 不在仓库内，按路由器 README 中的链接下载

## Windows 客户端，带直连模式

宿舍有线网通常是**静态分配**（绑定 MAC + 固定 IP + 网关，无 DHCP），且身份与路由器
共用。Windows 客户端内置「直连模式」：**在学校分配的参数基础上，替你完成网卡配置 +
认证两件事**，不用再手动改网卡的 IPv4 属性。

- 登录窗口「直连模式」分组里填：绑定 MAC（可自定义为路由器mac，也可以填写自己的网卡mac，但这些都要在网络中心申请分配！）、IP、掩码位（通常是24，对应255.255.255.0）、网关、DNS、网卡号；
- 勾选「登录时自动配置网卡并接管身份」→ 重启客户端（需要管理员权限）→ 登录；
- 客户端自动完成：克隆绑定 MAC、配静态 IP/网关/DNS、加认证服务器 /32 路由、WLAN 降为备用；
- 退出客户端自动还原网络配置；崩溃残留下次启动自动清理；
- 详细字段说明和 AI 带装提示词：[jlu-drcom-win-QT/README.md](jlu-drcom-win-QT/README.md)

**注意：直连模式与路由器不能同时在线**（同一个 MAC + 同一个 IP，冲突后两条线路一起断）。

## newclient.py配置说明

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
| `DrCOM_JLU_Qt_v1.0.0.8_direct.zip` | Windows 客户端（含 Qt 运行库，解压即用；直连参数在窗口里填） |
| `jlu-drcom-armv7` / `jlu-drcom-aarch64` | 路由器静态二进制（scp 到路由器后 `chmod +x`） |

## 免责声明

本项目仅供学习交流，请遵守所在学校/单位网络使用规定。使用本仓库任何工具造成的
后果由使用者自行承担。

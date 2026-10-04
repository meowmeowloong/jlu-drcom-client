# JLU DrCOM 红米路由器客户端

让路由器替你完成吉林大学校园网（DrCOM）认证：路由器开机自动登录并保活，
**手机、电脑连上 WiFi 直接上网，不用再各自认证**。

适用：红米 AX3000（RA81）等 aarch64 架构的小米/红米路由器，**保留原厂 MiWiFi 固件、不刷机**，
通过社区工具开启 SSH 后，塞入一个静态编译的 DrCOM 客户端。

---

## 不想看文档？让 AI 手把手带你装

把下面整段提示词复制给任意 AI 助手（豆包 / ChatGPT / Kimi…），它会一步步带你完成全部安装：

```
请扮演一位熟悉校园网和 OpenWrt 的网络工程师，带我在宿舍完成"路由器自动认证校园网"。
我的情况：
- 学校：吉林大学，认证方式 DrCOM，认证服务器 10.100.61.3，端口 61440
- 路由器：红米 AX3000（RA81），保留原厂 MiWiFi 固件，绝不刷机
- 我手上有一台 Windows 电脑（可以装 WSL）
- 我已经从学校网络中心拿到：校园网账号、密码，以及绑定好的 MAC
- 项目仓库：https://github.com/zf-666888/jlu-drcom-client （用 jlu-drcom-router 目录）

请按下面的顺序带我操作，每一步等我做完并确认后再继续：
1. 用 XMiR-Patcher 免刷机开启路由器 SSH（先提醒我风险和备份）
2. 在 Windows 里启用 WSL，下载 musl 交叉工具链 arm-linux-musleabi-cross
3. （可选）编译仓库里的静态二进制（sh jlu-drcom-router/scripts/build.sh，产物 bin/jlu-drcom-armv7）
4. 用 scp -O 把二进制传到路由器 /data/drcom/，然后运行 jlu-drcom-router/scripts/install.sh
5. 配置放在 /data/drcom/drcom.conf（注意：配置绝对不能放 /etc，原厂固件重启会清空 /etc）
6. 设置 crontab 每分钟守护，最后教我怎么在日志里确认"登录成功"

要求：所有命令都给我可以直接复制执行的版本；涉及账号密码的步骤提醒我别截图发群；
我贴报错时直接给修复命令，不要让我自己去搜。
```

装好之后哪天断了网，把路由器 `/data/drcom/drcom.log` 最后 30 行复制给 AI，
让它对照下面「故障排查」一节的表格帮你判断原因。

---

## 一、原理与风险（先读这段）

| 方案                      | 做法                                              | 结果                            |
| ------------------------- | ------------------------------------------------- | ------------------------------- |
| ❌ 管理界面的「安装插件」 | 小米官方插件市场，只认签名`.mpk`，无 DrCOM 插件 | **走不通**                |
| ✅ 本方案                 | 开启 SSH → 塞入静态二进制 → 开机自启            | 可行，但属于「root 你的路由器」 |

**重要提醒：**

- 开启 SSH 用的是**第三方社区工具**，不是小米官方功能，会**失去保修、存在变砖风险**，推荐闲鱼淘二手一个
- 部分固件版本封堵了 SSH 漏洞，可能要先**降级固件**。
- 原厂固件一旦**升级**，SSH 和脚本可能失效，需要重来；建议关闭自动升级。
- 请自行评估风险后再操作。

---

## 二、你需要准备的东西

1. 一台能跑 SSH 的电脑（Windows / Mac / Linux 都行，Windows 下用 `scp` 即可）。
2. 路由器背面贴纸确认型号是 **红米 AX3000 (RA81)**。
3. 校园网账号密码。

---

## 三、步骤总览

```
① 编译静态二进制  →  ② 开启 SSH  →  ③ 传文件到路由器  →  ④ 装并配置  →  ⑤ 开机自启
```

---

## 四、步骤 1：拿到 `jlu-drcom-aarch64` 二进制

拿到 `jlu-drcom-aarch64` 二进制

程序是纯 C + libc，没有任何第三方依赖。

**已经编译好了**：`bin/jlu-drcom-aarch64`（95KB，完全静态，无动态依赖，可直接运行）。

如果你要自己重新编译：

```bash
# 免 sudo 的方式（推荐）：下载 musl 交叉工具链
curl -O https://musl.cc/aarch64-linux-musl-cross.tgz
tar xzf aarch64-linux-musl-cross.tgz
export PATH=$HOME/aarch64-linux-musl-cross/bin:$PATH
sh scripts/build.sh

# 或者用系统包管理器（需要 sudo 密码）
sudo apt-get install -y gcc-aarch64-linux-gnu
sh scripts/build.sh
```

> Windows 用户可在 WSL 里执行。若 WSL 的 sudo 需要密码会卡住，用上面的 musl 方式免密码。

---

## 步骤 2：开启路由器 SSH（红米 AX3000 / RA81）

使用社区工具 **XMiR-Patcher**（一键开启 SSH，免拆机、不刷固件）：

1. 下载 XMiR-Patcher：[汉化版（ozero.top）](https://ozero.top/archives/xmir-patcher)，
   或 GitHub 原版。
2. Windows 下运行 `run.bat`，输入路由器 IP（通常 `192.168.31.1`）和后台登录密码，
   按提示选「开启 SSH」，约 3 分钟完成。
3. 完成后 `ssh root@192.168.31.1` 能登录即成功（默认密码通常是 `root`，以工具提示为准）。
4. 强烈建议在工具里再执行一次「**固化 SSH**」，否则重启后 SSH 会失效。

参考教程：

- [红米AX3000 免拆机开启SSH教程](https://www.luyouwang.net/14555.html)
- [小米/红米路由器解锁 SSH 教程（linux.do）](https://linux.do/t/topic/1170421/7)

> 如果 XMiR-Patcher 对你的固件版本不生效，可能需要先降级固件；不同固件版本成功率不同，
> 详见上面两篇教程的评论区。

---

## 步骤 3：把文件传到路由器

```bash
# 在路由器上建一个临时目录
ssh root@192.168.31.1 "mkdir -p /tmp/drcom"

# 从你的电脑上传（在克隆到本地的本仓库 jlu-drcom-router 目录下执行）
scp bin/jlu-drcom-aarch64        root@192.168.31.1:/tmp/drcom/
scp etc/drcom.conf               root@192.168.31.1:/tmp/drcom/
scp scripts/drcom-service.sh     root@192.168.31.1:/tmp/drcom/
scp scripts/install.sh           root@192.168.31.1:/tmp/drcom/
```

---

## 步骤 4：填配置并安装

**先在电脑上把配置填好**——复制模板再填账号密码（模板里是占位符，`drcom.conf` 已被
`.gitignore` 忽略，不会误提交密码）：

```bash
cp etc/drcom.conf.example etc/drcom.conf
# 然后编辑 etc/drcom.conf：
#   username  你的学号
#   password  你的密码
#   mac       0            # 账号绑定了 MAC 的话填那个 MAC，否则保持 0
```

然后登录路由器执行：

```bash
ssh root@192.168.31.1
sh /tmp/drcom/install.sh
```

安装脚本会自动：装二进制到 `/data/drcom/`、装配置到 `/etc/drcom.conf`、
写入开机自启（`/etc/rc.local`）+ 守护进程（crontab 每分钟检查）、并立即启动。

---

## 步骤 5：验证

```bash
# 看日志，出现「登录成功」即可
tail -f /data/drcom/drcom.log
```

看到 `[INFO] 登录成功` 后，手机/电脑连上路由器 WiFi 应该就能直接上网了。
可以关掉电脑上的 DrCOM 客户端测试。

常用命令：

```bash
sh /data/drcom/drcom-service.sh status    # 查看运行状态
sh /data/drcom/drcom-service.sh stop      # 停止
sh /data/drcom/drcom-service.sh restart   # 重启
tail -f /data/drcom/drcom.log             # 实时日志
```

---

## 五、关于 MAC 绑定（重要）

吉林大学 DrCOM 会把账号和 MAC 地址绑定。分两种情况：

1. **账号没绑 MAC**（大多数自助开通的）：配置里 `mac` 保持 `0` 即可，无需任何额外操作。
2. **账号绑了某台设备的 MAC**：
   - 把配置里的 `mac` 改成那个 MAC（`aa:bb:cc:dd:ee:ff` 格式）；
   - 同时建议把**路由器 WAN 口的 MAC 克隆**成那个 MAC——
     在 MiWiFi 后台「常用设置 → 上网设置 → MAC 地址克隆」里填，这样校园网 DHCP 分配 IP 时
     也不会因为 MAC 对不上而拒绝。

> 如果登录失败、日志里错误码是 `0x0b`（MAC 错误）或 `0x07`（IP 不匹配），先按上面的方法
> 处理 MAC 克隆。

---

## 六、故障排查

| 日志里的错误码          | 含义                       | 处理                                                  |
| ----------------------- | -------------------------- | ----------------------------------------------------- |
| `0x03`                | 用户名或密码错误           | 检查配置                                              |
| `0x0b`                | MAC 地址错误               | 见上一节 MAC 克隆                                     |
| `0x07` / `0x16`     | 账号只能在该 IP/MAC 上使用 | MAC 克隆 + 等 DHCP 分配正确 IP                        |
| `0x14`                | 该账号对应多个 IP          | 把其他已认证设备下线                                  |
| `0x15`                | 客户端版本过旧             | 联系网络中心                                          |
| `0x17`                | 必须用 DHCP 上网           | WAN 口改成 DHCP（自动获取）                           |
| 一直`challenge 超时`  | 服务器没响应               | 检查 WAN 口是否已从校园网拿到 IP、网线/上级网络是否通 |
| `绑定 61440 端口失败` | 端口被占用                 | `sh ... restart`，或重启路由器                      |

其他排查要点：

- 确认 WAN 口接的是**校园网那根线**，且 MiWiFi 后台「上网设置」是**静态 IP**：
  学校分配的静态 IP/掩码 + 网关（以学校实际分配为准），WAN 口 MAC 克隆为
  校园网绑定的 MAC。宿舍有线网**没有 DHCP**，DHCP 模式拿不到任何地址。
- 确认路由器本身能通过校园网（静态 IP 配好后，即使不认证，也能 ping 通网关）。
- 排查协议细节时，把配置里的 `debug` 改成 `1`，日志会打印每个报文的十六进制。

---

## 七、目录结构

```
jlu-drcom-router/
├── src/
│   ├── jlu-drcom.c        客户端主程序（POSIX socket + DrCOM 协议）
│   └── md5.c / md4.c / sha1.c / *.h   加密算法（公共领域实现）
├── etc/
│   └── drcom.conf.example 配置模板（复制成 drcom.conf 后填账号）
├── scripts/
│   ├── build.sh           交叉编译脚本
│   ├── install.sh         路由器安装脚本
│   ├── drcom-service.sh   服务脚本（start/stop/status）
│   └── laptop-direct.ps1  笔记本直连接管/归还脚本（Windows，需管理员）
├── test/
│   ├── fake_server.py     假认证服务器 + 独立 Python 参考实现
│   ├── drcom-test.conf    测试配置
│   └── run_test.sh        一键跑协议一致性测试
├── bin/
│   └── jlu-drcom-aarch64  编译好的静态二进制（95KB）
└── README.md
```

协议实现忠实移植自本仓库已知可用的 `jlu-drcom-win-QT/src/dogcom.cpp`，
协议细节见仓库根目录的 [jlu-drcom-protocol.md](../jlu-drcom-protocol.md)。

---

## 八、这个客户端验证过吗？

验证过，而且是**逐字节验证**，不是"能编译就算数"。

`test/` 下有一个假 DrCOM 服务器 + **一份用 Python 独立重写的参考实现**。
测试时客户端真实跑完 `challenge → login → keepalive_1 → keepalive_2` 全流程，
假服务器把客户端实际发出的登录包与 Python 参考实现算出的期望包**逐字节比对**：

```bash
sh test/run_test.sh
```

覆盖的用例：

| 用例      | 说明                          |
| --------- | ----------------------------- |
| 8 位密码  | 普通分支，登录包 338 字节     |
| 10 位密码 | 触发 JLU 长密码分支，370 字节 |
| 16 位密码 | JLU 分支边界，374 字节        |
| MAC 全 0  | 不校验 MAC 的账号             |

四个用例均 **逐字节完全一致**。这验证了 MD5A/MD5B、checksum1、checksum2、
ror、MAC 异或等所有加密与校验逻辑都与已知可用的 C++ 客户端一致。

> 注意：这验证的是**协议实现正确性**。真实校园网认证能否成功，最终还取决于
> 账号、MAC 绑定、网络环境等因素——见上面「故障排查」一节。

---

## 九、笔记本直连模式（断电夜 / 不开路由器）

**背景**：宿舍有线网是静态分配（绑定 MAC + 固定 IP + 网关），**没有 DHCP**。
路由器的 WAN 配的就是这套身份；路由器断电（宿舍晚间断电）时，笔记本可以临时
**接管同一套身份**直连墙口，用 QT 客户端认证上网。

**铁律：笔记本和路由器不能同时在线。** 同一个 MAC + 同一个 IP 同时上 = 地址冲突，
两条线路一起断。

### 推荐方式：QT 客户端内置直连模式（v1.0.0.7 起）

用 `jlu-drcom-win-QT/release/DrCOM-JLU-Windows-MinGW/deploy-windows-mingw/DrCOM_JLU_Qt.exe`
（v1.0.0.7，旧版 MSVC exe 已改名备份）：

1. 登录界面勾选 **「直连模式（接管校园网身份）」**——只需勾一次，客户端会记住；
2. 断电夜：路由器断电 → 网线插墙口 → 双击客户端（弹 UAC 点「是」）→ 登录；
3. 用完退出客户端 → 网络配置自动还原 → 网线插回路由器、上电。

客户端启动时自动完成：克隆绑定 MAC、配静态 IP/网关/DNS、加认证服务器 /32 路由、
WLAN 降为备用线路；退出时全部还原；崩溃强杀留下的残留下次启动自动清理。
日志里 `[DirectMode]` 前缀就是这套动作。平时连路由器时**不要**勾这个框。

### 备用方式：PowerShell 脚本（效果相同）

切换顺序：

| 场景                 | 操作顺序                                                       |
| -------------------- | -------------------------------------------------------------- |
| 断电夜 / 直连上网    | 路由器断电 → `takeover` → 打开 QT 客户端登录                     |
| 路由器恢复供电       | 退出 QT 客户端 → `restore` → 再给路由器上电                      |

管理员 PowerShell 运行：

```powershell
# 接管（路由器必须已断电！）
powershell -ExecutionPolicy Bypass -File scripts\laptop-direct.ps1 takeover
# 归还（路由器上电前必须先执行）
powershell -ExecutionPolicy Bypass -File scripts\laptop-direct.ps1 restore
```

`takeover` 做的事：把有线网卡 MAC 克隆成绑定 MAC → 配学校分配的静态 IP/掩码、网关 → 加一条认证服务器 `10.100.61.3/32` 的主机路由
（**这条很关键**：笔记本若同时连着热点/其他网络，没有它认证包会从错误的网卡发出去，
表现就是 `dhcp challenge failed`）→ 把 WLAN 降为备用线路。

`restore` 做相反的事：退客户端、删路由、恢复原 MAC、恢复 DHCP、WLAN 恢复自动度量。

排障备忘（2026-10-04 实测结论）：

- 直连失败先看 `ipconfig`：有线口若是 `169.254.x.x`（APIPA）说明在等一个不存在的 DHCP；
- 认证服务器 ping 不通不代表断网（它不回 ICMP），**TCP/网页能通就是通**；
- 脚本里的 IP/MAC 是当前分配值，学校改分配或换账号时需要同步修改。

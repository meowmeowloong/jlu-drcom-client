# JLU DrCOM 红米路由器客户端

让路由器替你完成吉林大学校园网（DrCOM）认证：路由器开机自动登录并保活，
**手机、电脑连上 WiFi 直接上网，不用再各自认证**。

适用：红米 AX3000（RA81）等小米/红米路由器，**保留原厂 MiWiFi 固件、不刷机**，
通过社区工具开启 SSH 后，塞入一个静态编译的 DrCOM 客户端。
注意架构：RA81 的用户态是 **32 位 ARMv7**（用 `bin/jlu-drcom-armv7`），
其他 64 位用户态的机型才用 `bin/jlu-drcom-aarch64`——塞错了会报 `Exec format error`。

---

## ⚡ 先记住两件事

**1. 宿舍晚上断电，不用管它。**
程序、配置都装在 `/data`（断电重启不丢），守护进程写在 crontab（重启后保留）。
来电后路由器自己开机，**1~2 分钟内自动重新认证，什么都不用做**。
超过 5 分钟还没网，再按「九、故障排查」处理。

**2. 断电夜急着上网：让笔记本顶上。**
把路由器断电（或拔掉它的网线），网线插墙口，用 Windows 客户端的「直连模式」登录，
见「十三、笔记本直连模式」。

> **铁律：笔记本直连和路由器，同一时间只能有一个在线。**
> 它们共用同一套校园网身份（同一个 MAC + 同一个 IP），同时在线 = 地址冲突，
> 两条线路一起断。路由器上电之前，先退出笔记本的直连客户端。

---

## 让 AI 手把手带你装（推荐，不想看文档就选这条）

**整个安装过程推荐直接交给 AI**：把下面整段提示词复制给任意 AI 助手
（豆包 / ChatGPT / Kimi…），它会一步步带你完成，比照着文档摸索省事得多：

```
请扮演一位熟悉校园网和 OpenWrt 的网络工程师，带我在宿舍完成"路由器自动认证校园网"。
我的情况：
- 学校：吉林大学，认证方式 DrCOM，认证服务器 10.100.61.3，UDP 端口 61440
- 路由器：红米 AX3000（RA81），保留原厂 MiWiFi 固件，绝不刷机（用户态是 32 位 ARMv7）
- 我手上有一台 Windows 电脑
- 我已经从学校网络中心拿到：校园网账号、密码，分配的静态 IP/掩码/网关，以及绑定的 MAC
- 项目仓库：https://github.com/zf-666888/jlu-drcom-client （用 jlu-drcom-router 目录）

请按下面的顺序带我操作，每一步等我做完并确认后再继续：
1. 从仓库下载预编译好的 bin/jlu-drcom-armv7，不用自己编译（除非我要改代码，再教我开 WSL 用 musl 工具链）
2. 用 XMiR-Patcher（GitHub 原版：openwrt-xiaomi/xmir-patcher）免刷机开启 SSH，
   并执行"固化 SSH"；开始前先提醒我变砖/失去保修的风险
3. 在 MiWiFi 后台把 WAN 口配成静态 IP（填学校分配的 IP/掩码/网关），
   并把 WAN 口 MAC 克隆成绑定的 MAC
4. 用 scp -O 把二进制、配置模板、drcom-service.sh、install.sh 传到路由器 /tmp/drcom
   （ssh/scp 连老 dropbear 报算法错误时，直接给我兼容参数）
5. SSH 登录后执行 sh /tmp/drcom/install.sh；确认配置最终装在 /data/drcom/
   （/etc 重启会被清空，绝不能放 /etc）
6. 教我用 tail -f /data/drcom/drcom.log 确认出现「登录成功」，再用手机连 WiFi 验证能上网

要求：所有命令都给我可以直接复制执行的版本；涉及账号密码的步骤提醒我别截图发群；
我贴报错时直接给修复命令，不要让我自己去搜。
```

装好之后哪天断了网，把路由器 `/data/drcom/drcom.log` 最后 30 行复制给 AI，
让它对照「九、故障排查」的表格帮你判断原因。

---

## 一、原理与风险（先读这段）

| 方案                      | 做法                                              | 结果                            |
| ------------------------- | ------------------------------------------------- | ------------------------------- |
| ❌ 管理界面的「安装插件」 | 小米官方插件市场，只认签名`.mpk`，无 DrCOM 插件 | **走不通**                |
| ✅ 本方案                 | 开启 SSH → 塞入静态二进制 → 开机自启            | 可行，但属于「root 你的路由器」 |

**重要提醒：**

- 开启 SSH 用的是**第三方社区工具**，不是小米官方功能，会**失去保修、存在变砖风险**，
  风险等级和刷机相当（只是没替换固件）；担心变砖的话，推荐闲鱼淘个二手同款来练手。
- 部分固件版本封堵了 SSH 漏洞，可能要先**降级固件**。
- 原厂固件一旦**升级**，SSH 和脚本可能失效，需要重来；建议关闭自动升级。
- 请自行评估风险后再操作。

---

## 二、你需要准备的东西

1. 一台电脑（Windows / Mac / Linux 都行，Windows 10+ 自带 `ssh` / `scp`）。
2. 路由器背面贴纸确认型号是 **红米 AX3000 (RA81)**（其他机型看「十、其他架构」）。
3. 校园网账号密码。
4. 网络中心分配的**静态 IP、掩码、网关**——吉大宿舍有线网**没有 DHCP**，
   全靠这套参数；还没分配的先去网络中心/自助平台申请。
5. 账号如果绑定了 MAC，把那个 MAC 也记下来。

---

## 三、步骤总览

```
① 开启 SSH → ② 配 WAN 口（静态 IP + MAC 克隆）→ ③ 传文件 → ④ 填配置并安装 → ⑤ 验证
```

二进制已经预编译好放在仓库 `bin/` 里，**默认不用自己编译**
（想自己编译见「十、其他架构 / 自己编译」）。

---

## 四、步骤 1：开启路由器 SSH（XMiR-Patcher）

使用社区工具 **XMiR-Patcher**（一键开启 SSH，免拆机、不刷固件）：

1. 下载 XMiR-Patcher：
   - GitHub 原版：**[openwrt-xiaomi/xmir-patcher](https://github.com/openwrt-xiaomi/xmir-patcher)**
   - [汉化版（ozero.top）](https://ozero.top/archives/xmir-patcher)
2. Windows 下运行 `run.bat`，输入路由器 IP（通常 `192.168.31.1`）和后台登录密码，
   按提示选「开启 SSH」，约 3 分钟完成。
3. 完成后 `ssh root@192.168.31.1` 能登录即成功（默认密码通常是 `root`，以工具提示为准）。
4. 强烈建议在工具里再执行一次「**固化 SSH**」（菜单 `8 - Other functions` → `7 - Install permanent SSH`），
   否则重启后 SSH 会失效。

参考教程：

- [红米AX3000 免拆机开启SSH教程](https://www.luyouwang.net/14555.html)
- [小米/红米路由器解锁 SSH 教程（linux.do）](https://linux.do/t/topic/1170421/7)

> **连不上 SSH？** 老固件的 dropbear 只支持旧算法，新版系统的 ssh 会报
> `no matching host key type found. Their offer: ssh-rsa`，改用兼容参数连接：
>
> ```bash
> ssh -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedAlgorithms=+ssh-rsa \
>     -o KexAlgorithms=+diffie-hellman-group14-sha1,diffie-hellman-group1-sha1 \
>     -o Ciphers=+aes128-cbc,3des-cbc -o MACs=+hmac-sha1 root@192.168.31.1
> ```
>
> 后文的 `scp` 记得加 `-O`（dropbear 不支持 SFTP 子系统）。

> 如果 XMiR-Patcher 对你的固件版本不生效，可能需要先降级固件；不同固件版本成功率不同，
> 详见上面两篇教程的评论区。

---

## 五、步骤 2：配置 WAN 口（静态 IP + MAC 克隆）

在 MiWiFi 后台（浏览器打开 `192.168.31.1`）→「常用设置 → 上网设置」：

1. 上网方式选**静态 IP**，填学校分配的 IP、子网掩码、网关。
   **不要选「自动获取（DHCP）」**——宿舍有线网没有 DHCP，选了永远拿不到地址。
2. **MAC 地址克隆**（按下面的表决定要不要做）。

**MAC 绑定规则**：吉大 DrCOM 服务端按（账号，IP，MAC）三元组校验，对号入座：

| 你的情况                                | WAN 口 MAC 克隆                              | 配置里的 `mac` 字段                |
| --------------------------------------- | -------------------------------------------- | ---------------------------------- |
| 账号没绑 MAC（多数自助开通的）          | 不用动                                       | 填 `0`                             |
| 账号绑定了某台设备的 MAC                | 「MAC 地址克隆」里克隆成那个 MAC             | 填那个 MAC（`aa:bb:cc:dd:ee:ff`）  |

配完静态 IP 后，即使还没认证，也应该能 ping 通网关（SSH 上去 `ping 网关IP` 试试）；
此时上不了外网是正常的，认证之前就是这样。

> 克隆没做或做错了，日志里会出现 `0x0b`（MAC 错误）或 `0x07` / `0x16`（IP/MAC 不匹配），
> 回到这一步检查。

---

## 六、步骤 3：把文件传到路由器

在克隆到本地的仓库 `jlu-drcom-router` 目录下执行（RA81 用 armv7；其他 64 位机型把文件名换成
`jlu-drcom-aarch64`）：

```bash
ssh root@192.168.31.1 "mkdir -p /tmp/drcom"

scp -O bin/jlu-drcom-armv7        root@192.168.31.1:/tmp/drcom/
scp -O etc/drcom.conf.example     root@192.168.31.1:/tmp/drcom/
scp -O scripts/drcom-service.sh   root@192.168.31.1:/tmp/drcom/
scp -O scripts/install.sh         root@192.168.31.1:/tmp/drcom/
```

> 新版 `scp` 报错就加 `-O`；scp 传完的二进制会丢执行位，下一步的安装脚本会自动 `chmod +x`。

---

## 七、步骤 4：填配置并安装

**先在电脑上把配置填好**——复制模板再填账号密码（模板里是占位符，`drcom.conf` 已被
`.gitignore` 忽略，不会误提交密码）：

```bash
cp etc/drcom.conf.example etc/drcom.conf
# 然后编辑 etc/drcom.conf：
#   username  你的学号
#   password  你的密码
#   mac       0            # 账号绑定了 MAC 就填那个 MAC（见步骤 2 的表格）
```

把填好的配置传上去，然后登录路由器执行安装：

```bash
scp -O etc/drcom.conf root@192.168.31.1:/tmp/drcom/
ssh root@192.168.31.1
sh /tmp/drcom/install.sh
```

安装脚本会自动：把二进制、配置、服务脚本装到 **`/data/drcom/`**（`/data` 重启不丢；
**千万别放 `/etc`**——原厂固件重启会清空 `/etc`）、写入开机自启 +
crontab 每分钟守护（挂了自动拉起），并立即启动。

---

## 八、步骤 5：验证

```bash
tail -f /data/drcom/drcom.log    # 出现「登录成功」即可
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

> 认证服务器 `10.100.61.3` **不回 ping**（ICMP），ping 不通不代表断网；
> **网页打得开就是通的**。

---

## 九、故障排查

| 日志里的错误码        | 含义                       | 处理                                                              |
| --------------------- | -------------------------- | ----------------------------------------------------------------- |
| `0x03`                | 用户名或密码错误           | 检查 `/data/drcom/drcom.conf`                                     |
| `0x0b`                | MAC 地址错误               | 见「步骤 2」的 MAC 克隆                                           |
| `0x07` / `0x16`       | 账号只能在该 IP/MAC 上使用 | WAN 口静态 IP 必须是分配的那个 + MAC 克隆（回到步骤 2）           |
| `0x14`                | 该账号对应多个 IP          | 把其他已认证设备下线                                              |
| `0x15`                | 客户端版本过旧             | 联系网络中心                                                      |
| `0x17`                | 服务器要求 DHCP 上网       | 吉大宿舍是静态分配，正常不会遇到；DHCP 环境把 WAN 口改回「自动获取」 |
| 一直`challenge 超时`  | 服务器没响应               | WAN 口没配静态 IP / 网线没插对 / 上级线路不通，回步骤 2 检查       |
| `绑定 61440 端口失败` | 端口被占用                 | `sh /data/drcom/drcom-service.sh restart`，或重启路由器           |

其他排查要点：

- 确认 WAN 口接的是**校园网那根线**，上网设置是**静态 IP**（步骤 2）。
  宿舍有线网没有 DHCP，选「自动获取」拿不到任何地址——
  直连电脑时看到 `169.254.x.x` 就是这个原因。
- **断电来电后超过 5 分钟还没自动恢复**：SSH 上去看
  `tail -30 /data/drcom/drcom.log`；若日志毫无动静，检查 crontab 还在不在
  （`crontab -l`）、配置还在不在（`/data/drcom/drcom.conf`），
  然后手动 `sh /data/drcom/drcom-service.sh restart`。
- 排查协议细节时，把配置里的 `debug` 改成 `1`，日志会打印每个报文的十六进制。

---

## 十、其他架构 / 自己编译

- **红米 AX3000 (RA81)**：SoC 是 64 位的 Cortex-A53，但原厂固件用户态是 **32 位 ARMv7 软浮点**
  （SSH 上去 `uname -m` 显示 `armv7l`）。必须用 `bin/jlu-drcom-armv7`；
  塞 aarch64 二进制会直接 `Exec format error`。
- 其他小米/红米机型：SSH 上去跑 `uname -m`——`armv7l` 用 armv7 版，`aarch64` 用 aarch64 版。

自己编译（在 Linux / WSL 里；Git Bash 不行）：

```bash
# armv7（RA81 用这个）
curl -O https://musl.cc/arm-linux-musleabi-cross.tgz
tar xzf arm-linux-musleabi-cross.tgz
export PATH=$HOME/arm-linux-musleabi-cross/bin:$PATH
sh scripts/build.sh

# aarch64（其他 64 位机型）
ARCH=aarch64 sh scripts/build.sh    # 工具链换 aarch64-linux-musl-cross，见 build.sh 头部注释
```

编译参数是 `-O2 -static -no-pie`：完全静态、非 PIE，兼容原厂老内核（Linux 4.4）。

---

## 十一、目录结构

```
jlu-drcom-router/
├── src/
│   ├── jlu-drcom.c        客户端主程序（POSIX socket + DrCOM 协议）
│   └── md5.c / md4.c / sha1.c / *.h   加密算法（公共领域实现）
├── etc/
│   └── drcom.conf.example 配置模板（复制成 drcom.conf 后填账号；drcom.conf 不入库）
├── scripts/
│   ├── build.sh           交叉编译脚本（默认 armv7，ARCH=aarch64 编 64 位）
│   ├── install.sh         路由器安装脚本
│   ├── drcom-service.sh   服务脚本（start/stop/status）
│   └── laptop-direct.ps1  笔记本直连接管/归还脚本（Windows，需管理员）
├── test/
│   ├── fake_server.py     假认证服务器 + 独立 Python 参考实现
│   ├── drcom-test.conf    测试配置
│   └── run_test.sh        一键跑协议一致性测试
├── bin/
│   ├── jlu-drcom-armv7    预编译静态二进制（红米 AX3000 / RA81 用这个）
│   └── jlu-drcom-aarch64  预编译静态二进制（64 位用户态的其他小米/红米机型）
└── README.md
```

协议实现忠实移植自本仓库已知可用的 `jlu-drcom-win-QT/src/dogcom.cpp`，
协议细节见仓库根目录的 [jlu-drcom-protocol.md](../jlu-drcom-protocol.md)。

---

## 十二、这个客户端验证过吗？

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

## 十三、笔记本直连模式（断电夜 / 不开路由器）

**背景**：宿舍有线网是静态分配（绑定 MAC + 固定 IP + 网关），**没有 DHCP**。
路由器的 WAN 配的就是这套身份；路由器断电（宿舍晚间断电）或不想开路由器时，
笔记本可以临时**接管同一套身份**直连墙口，用 Windows 客户端认证上网。

**铁律：笔记本和路由器不能同时在线。** 同一个 MAC + 同一个 IP 同时上 = 地址冲突，
两条线路一起断。

### 推荐方式：Windows 客户端内置直连模式（v1.0.0.8 起）

下载：[Release v1.0.0.8](https://github.com/zf-666888/jlu-drcom-client/releases/tag/v1.0.0.8)
（`DrCOM_JLU_Qt_v1.0.0.8_direct.zip`，解压即用）。

1. 把网络中心分配的参数填进登录窗口的「直连模式」一栏：**IP、掩码位（一般 24）、
   网关、DNS、有线网卡编号**（在 `ipconfig` / 设备管理器里看是第几块网卡）；
2. 勾选「直连模式」——首次勾选后**重启客户端生效**（它需要管理员权限，UAC 弹窗点「是」）；
3. 断电夜：路由器断电 → 网线插墙口 → 双击客户端 → Login。
   客户端自动完成：克隆绑定 MAC、配静态 IP/网关/DNS、加认证服务器 /32 主机路由
   （**这条很关键**：笔记本若同时连着热点/其他网络，没有它认证包会从错误的网卡发出去，
   表现就是 `dhcp challenge failed`）、WLAN 降为备用线路；
4. 用完退出客户端 → 网络配置自动还原 → 网线插回路由器、上电。

崩溃/强杀留下的网络残留，下次启动会自动清理；日志里 `[DirectMode]` 前缀就是这套动作。
平时连路由器上网时，**不要**勾这个框。

### 备用方式：PowerShell 脚本（效果相同）

`scripts/laptop-direct.ps1` 开头是占位参数，**先改成你自己分配到的
IP/MAC/网关**，然后按下面的顺序操作：

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

`takeover` 做的事：把有线网卡 MAC 克隆成绑定 MAC → 配学校分配的静态 IP/掩码、网关 →
加一条认证服务器 `10.100.61.3/32` 的主机路由 → 把 WLAN 降为备用线路。
`restore` 做相反的事。

排障备忘（实测结论）：

- 直连失败先看 `ipconfig`：有线口若是 `169.254.x.x`（APIPA）说明在等一个不存在的 DHCP；
- 认证服务器 ping 不通不代表断网（它不回 ICMP），**TCP/网页能通就是通**；
- 学校改分配或换账号时，记得同步更新脚本里的 IP/MAC。

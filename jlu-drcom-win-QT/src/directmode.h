#ifndef DIRECTMODE_H
#define DIRECTMODE_H

// 直连模式：笔记本临时接管路由器的校园网身份（克隆 MAC + 静态 IP + 认证路由），
// 让 QT 客户端不依赖外部脚本就能在「网线直连宿舍墙口（路由器断电）」场景下上网。
//
// 使用方式：主界面勾选「直连模式」→ 重启客户端（会请求管理员权限）→ 登录。
// 退出客户端时自动还原；崩溃残留的配置会在下次启动时自动清理。
//
// 背景与原理见仓库 README（jlu-drcom-router/README.md 第九节），
// 参数与 jlu-drcom-router/scripts/laptop-direct.ps1 保持一致。

namespace DirectMode {

// 当前进程是否持有管理员令牌
bool isElevated();

// 以管理员权限重启自身（触发 UAC）。成功返回 true，调用方应立即退出。
bool relaunchElevated();

// 接管校园网身份（幂等）：克隆 MAC -> 静态 IP/网关/DNS -> 认证服务器 /32 路由
// -> WLAN 降为备用线路，并在 QSettings 写下 directActive 标记。
// 需要管理员权限。
bool takeover();

// 还原网络配置（幂等）：逆序撤销 takeover 的一切，清除 directActive 标记。
// 需要管理员权限。整个还原在**一个** PowerShell 进程里跑完，同步等待结束。
bool restore();

// restore 的异步版：立即清除 directActive 标记，把还原丢给一个后台 PowerShell
// 进程后马上返回，调用方（退出流程）不用等十几秒。输出记到
// %TEMP%\drcom-direct-restore.log。需要管理员权限（在提权进程里调用）。
bool restoreAsync();

} // namespace DirectMode

#endif // DIRECTMODE_H

#include "directmode.h"

#include <QCoreApplication>
#include <QProcess>
#include <QSettings>
#include <QThread>
#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include "constants.h"

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#endif

// ===== 校园网参数：注册表可配，默认值是占位符，必须填成你自己的才生效 =====
// 宿舍有线网是静态分配（绑定 MAC + 固定 IP + 网关），没有 DHCP，参数因人而异，
// 因此不硬编码进源码。启动时从注册表 HKCU\Software\DrCOM_JLU_Qt\OrganizationDefaults 读取：
//   direct/wiredIdx  有线网卡 ifIndex（Get-NetAdapter 查看）
//   direct/wlanIdx   无线网卡 ifIndex（接管期间降为备用线路）
//   direct/mac       校园网绑定的 MAC（路由器 WAN 克隆的那个）
//   direct/origMac   本机有线网卡原 MAC（restore 时还原用）
//   direct/ip        学校分配的静态 IP
//   direct/gw        网关
//   direct/dns1      DNS 1
//   direct/dns2      DNS 2
// 也可以直接改下面的默认值后重新编译。注意：真实参数因人而异，不要提交到公共仓库。
struct DirectParams {
	int     wiredIdx = 3;
	int     wlanIdx  = 4;
	QString mac      = "00:11:22:33:44:55";   // 占位符：校园网绑定 MAC
	QString origMac  = "";                     // 本机原 MAC（留空则 restore 时跳过 MAC 还原）
	QString ip       = "10.9.8.7";             // 占位符：学校分配的静态 IP (/24)
	QString gw       = "10.9.8.254";           // 占位符：网关
	QString dns1     = "223.5.5.5";
	QString dns2     = "119.29.29.29";
	int     prefix   = 24;
	DirectParams() {
		QSettings s(SETTINGS_FILE_NAME);
		wiredIdx = s.value("direct/wiredIdx", wiredIdx).toInt();
		wlanIdx  = s.value("direct/wlanIdx",  wlanIdx ).toInt();
		mac      = s.value("direct/mac",      mac     ).toString();
		origMac  = s.value("direct/origMac",  origMac ).toString();
		ip       = s.value("direct/ip",       ip      ).toString();
		gw       = s.value("direct/gw",       gw      ).toString();
		dns1     = s.value("direct/dns1",     dns1    ).toString();
		dns2     = s.value("direct/dns2",     dns2    ).toString();
		prefix   = s.value("direct/prefix",   prefix  ).toInt();
	}
};
static const DirectParams &P()
{
	static const DirectParams p;
	return p;
}

static bool runPS(const QString &cmd)
{
	QProcess p;
	p.start("powershell", QStringList{"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", cmd});
	if (!p.waitForStarted(5000)) {
		qDebug() << "[DirectMode] powershell failed to start:" << cmd;
		return false;
	}
	p.waitForFinished(60000);
	const QString out = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
	const QString err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
	if (!out.isEmpty())
		qDebug() << "[DirectMode] PS:" << out;
	if (p.exitCode() != 0) {
		qDebug() << "[DirectMode] PS exit" << p.exitCode() << ":" << err;
		return false;
	}
	return true;
}

static QString psOut(const QString &cmd)
{
	QProcess p;
	p.start("powershell", QStringList{"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", cmd});
	p.waitForFinished(15000);
	return QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
}

// 等网卡回到 Up 状态：在**一个** PowerShell 进程里轮询，最多 maxMs 毫秒，
// 起来就提前返回（改 MAC 会触发网卡重启，之前是死等 12 秒，实际通常 3~5 秒就回来了）
static void waitAdapterUpPS(int idx, int maxMs)
{
	runPS(QString("$t=[Diagnostics.Stopwatch]::StartNew(); "
				  "while($t.ElapsedMilliseconds -lt %2){ "
				  "if((Get-NetAdapter -InterfaceIndex %1 -ErrorAction SilentlyContinue).Status -eq 'Up'){break}; "
				  "Start-Sleep -Milliseconds 400 }")
			  .arg(idx).arg(maxMs));
}

bool DirectMode::isElevated()
{
#ifdef Q_OS_WIN
	DWORD elevated = 0;   // TOKEN_ELEVATION.TokenIsElevated
	DWORD retLen = 0;
	HANDLE token = nullptr;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		// 注意：ReturnLength 必须传有效指针（传 nullptr 在部分 Windows 版本上
		// 会让 API 失败，导致提权进程被误判为未提权 -> 无限自我提权重启）
		if (GetTokenInformation(token, TokenElevation, &elevated, sizeof(elevated), &retLen))
			return elevated != 0;
		qDebug() << "[DirectMode] GetTokenInformation failed, err =" << GetLastError();
		CloseHandle(token);
	} else {
		qDebug() << "[DirectMode] OpenProcessToken failed, err =" << GetLastError();
	}
	return false;
#else
	return true;
#endif
}

bool DirectMode::relaunchElevated()
{
#ifdef Q_OS_WIN
	// ShellExecuteExW 依赖 COM。本函数在 QApplication 构造之前被调用
	// （Qt 此时还没初始化 COM），必须自己初始化，否则调用会永久挂起。
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const QString exe = QCoreApplication::applicationFilePath();
	SHELLEXECUTEINFOW sei;
	ZeroMemory(&sei, sizeof(sei));
	sei.cbSize = sizeof(sei);
	sei.lpVerb = L"runas";
	sei.lpFile = reinterpret_cast<LPCWSTR>(exe.utf16());
	sei.lpParameters = L"--direct-elevated";   // 防御性标记：子进程最多再重启一次
	sei.nShow  = SW_SHOWNORMAL;
	bool ok = ShellExecuteExW(&sei) != 0;
	if (!ok)
		qDebug() << "[DirectMode] relaunch elevated failed, err =" << GetLastError();
	CoUninitialize();
	return ok;
#endif
	return false;
}

bool DirectMode::takeover()
{
	qDebug() << "[DirectMode] takeover: begin";

	// 一开始就置位：接管进行中客户端若崩溃/被强杀，下次启动仍能识别残留并清理。
	// （放在最后置位的话，接管到一半就退出 = 网络改了一半且无人认领）
	QSettings sActive(SETTINGS_FILE_NAME);
	sActive.setValue(ID_DIRECT_ACTIVE, true);

	// 1. 克隆 MAC（改 MAC 会触发网卡重启，等它回来）
	const QString curMac = psOut(QString("(Get-NetAdapter -InterfaceIndex %1).MacAddress").arg(P().wiredIdx));
	if (curMac.compare(P().mac, Qt::CaseInsensitive) != 0) {
		runPS(QString("Get-NetAdapter -InterfaceIndex %1 | Set-NetAdapter -MacAddress '%2' -Confirm:$false")
					  .arg(P().wiredIdx).arg(P().mac));
		// 改 MAC 会触发网卡重启：轮询等它回来（最多 12 秒，通常 3~5 秒）
		waitAdapterUpPS(P().wiredIdx, 12000);
	} else {
		qDebug() << "[DirectMode] MAC already" << P().mac << ", skip cloning";
	}

	// 2. 静态 IP + 网关 + DNS（宿舍有线网没有 DHCP）。
	//    带验证重试：网卡刚重启后 New-NetIPAddress 可能暂时失败（错误 87），
	//    且 -ErrorAction SilentlyContinue 会把失败吞掉，所以每轮都核实最终状态。
	runPS(QString("Set-NetIPInterface -InterfaceIndex %1 -Dhcp Disabled -ErrorAction SilentlyContinue")
			  .arg(P().wiredIdx));
	for (int attempt = 1; attempt <= 3; ++attempt) {
		const QString curIp = psOut(QString("(Get-NetIPAddress -InterfaceIndex %1 -AddressFamily IPv4"
											" -ErrorAction SilentlyContinue).IPAddress").arg(P().wiredIdx));
		if (curIp.split('\n').first().trimmed().compare(P().ip, Qt::CaseInsensitive) == 0) {
			qDebug() << "[DirectMode] static IP already configured:" << P().ip;
			break;
		}
		qDebug() << "[DirectMode] configuring static IP, attempt" << attempt
				 << "current:" << QString(curIp).replace('\n', ' ');
		// 先清掉旧的默认网关路由：若上次残留了 0.0.0.0/0 路由，
		// New-NetIPAddress 会报 "Instance DefaultGateway already exists"(错误 87) 而整条失败
		runPS(QString("Remove-NetRoute -InterfaceIndex %1 -DestinationPrefix 0.0.0.0/0"
					  " -Confirm:$false -ErrorAction SilentlyContinue").arg(P().wiredIdx));
		runPS(QString("Remove-NetIPAddress -InterfaceIndex %1 -AddressFamily IPv4 -Confirm:$false"
					  " -ErrorAction SilentlyContinue").arg(P().wiredIdx));
		if (!runPS(QString("New-NetIPAddress -InterfaceIndex %1 -IPAddress %2 -PrefixLength %4"
						   " -DefaultGateway %3").arg(P().wiredIdx).arg(P().ip).arg(P().gw).arg(P().prefix))) {
			qDebug() << "[DirectMode] New-NetIPAddress failed, will retry";
		}
		waitAdapterUpPS(P().wiredIdx, 6000);
	}
	runPS(QString("Set-DnsClientServerAddress -InterfaceIndex %1 -ServerAddresses '%2','%3'")
			  .arg(P().wiredIdx).arg(P().dns1).arg(P().dns2));

	// 3. 认证服务器 /32 主机路由：笔记本同时连着热点/其他网络时，
	//    没有这条路由认证包会从错误的网卡发出去，表现为 challenge 无响应
	runPS(QString("Remove-NetRoute -DestinationPrefix %1/32 -Confirm:$false -ErrorAction SilentlyContinue; "
				  "New-NetRoute -DestinationPrefix %1/32 -InterfaceIndex %2 -NextHop %3 -RouteMetric 1"
				  " -ErrorAction SilentlyContinue")
			  .arg(SERVER_IP).arg(P().wiredIdx).arg(P().gw));

	// 4. WLAN 降为备用线路，避免流量继续走热点
	runPS(QString("Set-NetIPInterface -InterfaceIndex %1 -InterfaceMetric 5000 -ErrorAction SilentlyContinue")
			  .arg(P().wlanIdx));

	qDebug() << "[DirectMode] takeover: done";
	return true;
}

// ===== 还原脚本的各步骤合并成**一个** PowerShell 脚本 =====
// 旧版 restore 在主线程里串行拉起 7 个 powershell 进程 + 死等 6 秒，
// 退出时界面卡六七秒、托盘无响应。合并后：1 个进程，等待改成"轮询网卡回 Up"。
// 脚本内容保持纯 ASCII——PS 5.1 会把无 BOM 的非 ASCII 按 GBK 解析导致报错。
static QString buildRestoreScript()
{
	const DirectParams &p = P();
	QString s;
	s += "$ErrorActionPreference = 'SilentlyContinue'\n";
	s += "Start-Transcript -Path \"$env:TEMP\\drcom-direct-restore.log\" -Force | Out-Null\n";
	// 认证服务器 /32 主机路由
	s += QString("Remove-NetRoute -DestinationPrefix %1/32 -Confirm:$false\n").arg(SERVER_IP);
	if (!p.origMac.isEmpty()) {
		// 还原原 MAC（会触发网卡重启）
		s += QString("Get-NetAdapter -InterfaceIndex %1 | Set-NetAdapter -MacAddress '%2' -Confirm:$false\n")
				 .arg(p.wiredIdx).arg(p.origMac);
		// 轮询等网卡回来（最多 8 秒），别死等
		s += QString("foreach($i in 1..16){ "
					 "if((Get-NetAdapter -InterfaceIndex %1 -ErrorAction SilentlyContinue).Status -eq 'Up'){break}; "
					 "Start-Sleep -Milliseconds 500 }\n").arg(p.wiredIdx);
	} else {
		s += "Start-Sleep -Seconds 1\n";
	}
	// 静态 IP 移除 + 恢复 DHCP/DNS + WLAN 恢复自动度量 + 网卡重启归位
	s += QString("Remove-NetIPAddress -InterfaceIndex %1 -IPAddress %2 -Confirm:$false\n")
			 .arg(p.wiredIdx).arg(p.ip);
	s += QString("Set-NetIPInterface -InterfaceIndex %1 -Dhcp Enabled\n").arg(p.wiredIdx);
	s += QString("Set-DnsClientServerAddress -InterfaceIndex %1 -ResetServerAddresses\n").arg(p.wiredIdx);
	s += QString("Set-NetIPInterface -InterfaceIndex %1 -AutomaticMetric Enabled\n").arg(p.wlanIdx);
	s += QString("Get-NetAdapter -InterfaceIndex %1 | Restart-NetAdapter\n").arg(p.wiredIdx);
	s += "Stop-Transcript | Out-Null\n";
	return s;
}

static QString writeRestoreScript()
{
	const QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
							 + "/drcom-direct-restore.ps1";
	QFile f(path);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		f.write("\xEF\xBB\xBF");   // UTF-8 BOM，防止 PS 5.1 误判编码
		f.write(buildRestoreScript().toUtf8());
	} else {
		qDebug() << "[DirectMode] failed to write restore script:" << path;
	}
	return path;
}

bool DirectMode::restore()
{
	qDebug() << "[DirectMode] restore: begin (single powershell process)";

	// 先清标记保证幂等（退出流程可能触发两次 QuitDrcom）
	QSettings s(SETTINGS_FILE_NAME);
	if (!s.value(ID_DIRECT_ACTIVE, false).toBool()) {
		qDebug() << "[DirectMode] restore: not active, skip";
		return true;
	}
	s.setValue(ID_DIRECT_ACTIVE, false);

	const QString script = writeRestoreScript();
	QProcess p;
	p.start("powershell", QStringList{"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script});
	if (!p.waitForStarted(5000)) {
		qDebug() << "[DirectMode] restore: powershell failed to start";
		return false;
	}
	p.waitForFinished(60000);
	qDebug() << "[DirectMode] restore: done, exit" << p.exitCode()
			 << "(输出见 %TEMP%\\drcom-direct-restore.log)";
	return p.exitCode() == 0;
}

bool DirectMode::restoreAsync()
{
	// 先清标记保证幂等；就算后台还原进程被杀，用户也能用
	// scripts/laptop-direct.ps1 restore 手工兜底
	QSettings s(SETTINGS_FILE_NAME);
	if (!s.value(ID_DIRECT_ACTIVE, false).toBool())
		return false;
	s.setValue(ID_DIRECT_ACTIVE, false);

	const QString script = writeRestoreScript();
	qint64 pid = 0;
	const bool ok = QProcess::startDetached(
		"powershell",
		QStringList{"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script},
		QCoreApplication::applicationDirPath(), &pid);
	qDebug() << "[DirectMode] restoreAsync: detached ok=" << ok << "pid=" << pid
			 << "- restoring in background, log: %TEMP%\\drcom-direct-restore.log";
	return ok;
}

#include "mainwindow.h"
#include "directmode.h"
#include <singleapplication.h>
#include <QTranslator>
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <QDir>

static QString timePoint;

//日志生成
void LogMsgOutput(QtMsgType type,
	const QMessageLogContext&,
	const QString& msg)
{
	static QMutex mutex; //日志代码互斥锁

	// 持有锁
	mutex.lock();

	// Critical Resource of Code
	QByteArray localMsg = msg.toLocal8Bit();
	QString log;

	switch (type) {
	case QtDebugMsg:
		log.append(QString("Debug %1")
			.arg(localMsg.constData()));
		break;
	case QtInfoMsg:
		log.append(QString("Info: %1")
			.arg(localMsg.constData()));
		break;
	case QtWarningMsg:
		log.append(QString("Warning: %1")
			.arg(localMsg.constData()));
		break;
	case QtCriticalMsg:
		log.append(QString("Critical: %1")
			.arg(localMsg.constData()));
		break;
	case QtFatalMsg:
		log.append(QString("Fatal: %1")
			.arg(localMsg.constData()));
		break;
	}

	QDir dir(QApplication::applicationDirPath());
	dir.mkdir("logs");
	QFile file(dir.path() + QString("/logs/log%1.lgt").arg(timePoint));
	file.open(QIODevice::WriteOnly | QIODevice::Append);
	QTextStream out(&file);
    out << log << Qt::endl;
	file.close();

	// 释放锁
	mutex.unlock();
}

int main(int argc, char *argv[])
{
	// 直连模式：需要管理员权限改网络配置。
	// 1) 开了直连模式但没提权 -> 以管理员重启自身；
	// 2) directActive 残留（上次崩溃/强杀/接管中退出）-> 提权清理网络配置残留。
	// 注意要在 SingleApplication 构造之前做，避免提权重启的实例变成"第二个实例"。
	{
		// 防御性护栏：提权重启出来的子进程带 --direct-elevated 标记，最多重启一次，
		// 防止 isElevated() 异常时陷入无限自我提权循环
		const bool relaunchedAlready = SingleApplication::arguments().contains("--direct-elevated");
		QSettings s0(SETTINGS_FILE_NAME);
		const bool directMode   = s0.value(ID_DIRECT_MODE,   false).toBool();
		const bool directActive = s0.value(ID_DIRECT_ACTIVE, false).toBool();
		if ((directMode || directActive) && !relaunchedAlready && !DirectMode::isElevated()) {
			qDebug() << "direct mode on or leftover config present but not elevated, relaunching as admin...";
			if (DirectMode::relaunchElevated())
				return 0;
			// UAC 被拒绝则按普通模式继续，登录时会提示需要管理员
		}
		if (directActive) {
			// v1.0.0.9：后台清理，不阻塞启动；与 directMode 开关无关——
			// 标记为 true 只说明上次没有正常还原
			qDebug() << "leftover direct-mode config detected, restoring in background...";
			if (DirectMode::isElevated())
				DirectMode::restoreAsync();
			else
				qDebug() << "cleanup needs admin, relaunch declined";
		}
	}

	Q_INIT_RESOURCE(DrCOM_JLU_Qt);
	SingleApplication a(argc, argv);

	//release模式输出日志到文件
	// 因为调用了QApplication::applicationDirPath()
	// 要在QApplication实例化之后调用
#ifndef QT_DEBUG
	timePoint = QDateTime::currentDateTime().toString("yyyyMMddHHmmss");
	qInstallMessageHandler(LogMsgOutput);
#endif

	SingleApplication::setQuitOnLastWindowClosed(false);
    // SingleApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

	qDebug() << "...main...";

	QFont font = a.font();
	font.setPointSize(10);
	font.setFamily("Microsoft YaHei");
	a.setFont(font);

	QTranslator translator;
    if (!translator.load(":/ts/DrCOM_zh_CN.qm")) {
        qWarning() << "Failed to load translation file!";
    }
	a.installTranslator(&translator);

    MainWindow w(&a);
	QObject::connect(&a, &SingleApplication::instanceStarted, [&w]() {
		qDebug() << "One instance had started. Its window will be shown by the next line of the source code.";
		w.ShowLoginWindow();
	});

    QSettings s(SETTINGS_FILE_NAME);
    bool bHideWindow=s.value(ID_HIDE_WINDOW, false).toBool();
	// 如果是软件自行重启的就不显示窗口
	int restartTimes = s.value(ID_RESTART_TIMES, 0).toInt();
	qDebug() << "main: restartTimes=" << restartTimes;
    if(bHideWindow){
        qDebug()<<"not show window caused by user settings";
    } else if (restartTimes > 0) {
        // 是自行重启不显示窗口
        qDebug()<<"not show window caused by self restart";
    } else {
		qDebug() << "show caused by normal start";
		w.show();
	}
	return a.exec();
}

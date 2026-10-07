#include "dogcomcontroller.h"
#include <QDebug>

DogcomController::DogcomController()
{
	sleeper = new InterruptibleSleeper();
	dogcom = new DogCom(sleeper);

	connect(dogcom, &DogCom::ReportOnline, this, &DogcomController::HandleDogcomOnline);
	connect(dogcom, &DogCom::ReportOffline, this, &DogcomController::HandleDogcomOffline);
	connect(dogcom, &DogCom::ReportIpAddress, this, &DogcomController::HandleIpAddress);
}

DogcomController::~DogcomController(){
	// 线程可能还在 challenge/keepalive 里跑着（退出时机不固定）：
	// 先中断再等它退出，直接 delete 运行中的 QThread 会触发
	// "QThread: Destroyed while thread is still running" fatal
	if(dogcom!=nullptr){
		dogcom->Stop();
		dogcom->wait(5000);
		delete dogcom;
	}
	if(sleeper!=nullptr) delete sleeper;
}

void DogcomController::Login(const QString &account, const QString &password, const QString &mac_addr) {
	qDebug() << "Filling config...";
	dogcom->FillConfig(account, password, mac_addr);
	qDebug() << "Fill config done.";
	dogcom->start();
}

void DogcomController::LogOut()
{
	dogcom->Stop();
}

void DogcomController::HandleDogcomOffline(int reason)
{
	emit HaveBeenOffline(reason);
}

void DogcomController::HandleDogcomOnline()
{
	emit HaveLoggedIn();
}

void DogcomController::HandleIpAddress(unsigned char x1, unsigned char x2, unsigned char x3, unsigned char x4)
{
	QString ip = QString::asprintf("%d.%d.%d.%d", x1, x2, x3, x4);
	qDebug() << "IP ADDRESS:" << ip;
	emit HaveObtainedIp(ip);
}



#-------------------------------------------------
#
# Project created by QtCreator 2019-03-03T13:30:28
#
#-------------------------------------------------

QT       += core gui network widgets concurrent
# 自定义 rc：图标 + requireAdministrator 清单（直连模式需要管理员权限，双击即静默提权）
RC_FILE  = app.rc

# translations
TRANSLATIONS += ts/DrCOM_zh_CN.ts

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = DrCOM_JLU_Qt
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++11

SOURCES += \
    encrypt/EncryptData.cpp \
    main.cpp \
    mainwindow.cpp \
    dogcomcontroller.cpp \
    interruptiblesleeper.cpp \
    dogcom.cpp \
    encrypt/md4.cpp \
    encrypt/md5.cpp \
    encrypt/sha1.cpp \
    DogcomSocket.cpp \
    directmode.cpp

HEADERS += \
    encrypt/EncryptData.h \
    mainwindow.h \
    dogcomcontroller.h \
    constants.h \
    interruptiblesleeper.h \
    dogcom.h \
    encrypt/md4.h \
    encrypt/md5.h \
    encrypt/sha1.h \
    DogcomSocket.h \
    directmode.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    DrCOM_JLU_Qt.qrc

# Single Application implementation
include(singleinstance/singleapplication.pri)
DEFINES += QAPPLICATION_CLASS=QApplication

VERSION = 1.0.0.9

win32:LIBS += -lwsock32
win32:LIBS += -lcrypt32
win32:LIBS += -lshell32
win32:LIBS += -ladvapi32
win32:LIBS += -lole32
# 更新日志：
# v 0.0.0.0 实现基本功能
# v 1.0.0.1 修复适配高DPI时只窗口大小适配但字号不适配的bug
# v 1.0.0.2 增加重启功能（能解决一些网络的错误
#           调整字体为微软雅黑10号（就是win下正常的字体
# v 1.0.0.3 没有这个版本，上次该发布0.2版本时候压缩包名字打错了。。。应该为1.0.0.2的，所以跳过这个版本号
# v 1.0.0.4 优化用户体验，调整掉线时的提示信息，增加掉线时直接重启客户端的提示
# v 1.0.0.5 解决不稳定的bug，自动重启客户端重新登录，新增日志功能，方便查错
# v 1.0.0.6 更换QUdpSocket为win和linux原生接口，增强稳定性（并不知道为什么QUdpSocket要设置一个状态
#           增加了两个checkbox可选关闭校园网之窗和隐藏登录窗口
# v 1.0.0.7 新增直连模式：勾选后重启客户端（请求管理员权限），登录前自动接管校园网身份
#           （克隆绑定MAC+静态IP+认证服务器/32路由+WLAN降为备用），退出时自动还原，
#           崩溃残留下次启动自动清理。用于宿舍有线静态分配网+路由器断电场景，免外部脚本
# v 1.0.0.8 直连参数改为登录窗口内直接填写（IP/掩码/网关/DNS/网卡号），不再需要改注册表；
#           掩码位数可配；其余不变
# v 1.0.0.9 修复退出卡顿六七秒：网络还原合并为单个 PowerShell 进程并放到后台执行
#           （窗口先隐藏，体感秒退；还原日志在 %TEMP%\drcom-direct-restore.log）；
#           登录时的网卡接管挪到后台线程，界面不再冻结十几秒；等网卡改成轮询不再死等

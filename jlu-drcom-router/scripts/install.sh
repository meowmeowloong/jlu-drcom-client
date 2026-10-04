#!/bin/sh
# DrCOM 路由器安装脚本（在路由器上，通过 SSH 运行）
#
# 前置条件: 已经用 scp 把下面这些文件传到路由器（比如 /tmp/drcom）:
#   - jlu-drcom-armv7     （编译好的二进制）
#   - drcom.conf          （填好账号密码的配置）
#   - drcom-service.sh    （服务脚本）
#   - install.sh          （本脚本）
#
# 在路由器上执行:
#   sh /tmp/drcom/install.sh
#
# 本脚本会:
#   1. 把二进制装到 /data/drcom/drcom（/data 分区重启后不丢）
#   2. 把配置装到 /data/drcom/drcom.conf（/etc 重启会被原厂固件清空，必须放 /data）
#   3. 把服务脚本装到 /data/drcom/drcom-service.sh
#   4. 写入开机自启（/etc/rc.local）+ 守护进程（crontab 每分钟检查）—— /etc/rc.local 重启会被清空，真正兜底靠 crontab
#   5. 立即启动

set -e

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR=/data/drcom
BIN=$BIN_DIR/drcom
SERVICE=$BIN_DIR/drcom-service.sh
CONF=$BIN_DIR/drcom.conf

echo "==> 1/5 创建目录并安装二进制"
mkdir -p "$BIN_DIR"
# 按优先级找二进制：armv7（红米 AX3000 实际架构）优先，兼容 aarch64 命名
if [ -f "$SRC_DIR/jlu-drcom-armv7" ]; then
    BIN_SRC="$SRC_DIR/jlu-drcom-armv7"
elif [ -f "$SRC_DIR/jlu-drcom-aarch64" ]; then
    BIN_SRC="$SRC_DIR/jlu-drcom-aarch64"
else
    echo "    错误：找不到 jlu-drcom-armv7 或 jlu-drcom-aarch64" >&2
    exit 1
fi
cp "$BIN_SRC" "$BIN"
chmod +x "$BIN"
echo "    二进制: $BIN  (来自 $(basename "$BIN_SRC"))"

echo "==> 2/5 安装服务脚本"
cp "$SRC_DIR/drcom-service.sh" "$SERVICE"
chmod +x "$SERVICE"

echo "==> 3/5 安装配置"
# 优先用已填好账号的 drcom.conf；否则退回模板 drcom.conf.example
if [ -f "$SRC_DIR/drcom.conf" ]; then
    SRC_CONF="$SRC_DIR/drcom.conf"
elif [ -f "$SRC_DIR/drcom.conf.example" ]; then
    SRC_CONF="$SRC_DIR/drcom.conf.example"
else
    echo "    错误：找不到 drcom.conf 或 drcom.conf.example"
    exit 1
fi

# 如果路由器上已有填好的配置，保留；否则安装
if [ -f "$CONF" ] && ! grep -q "在这里填你的学号" "$CONF" 2>/dev/null; then
    echo "    检测到已有配置，保留 $CONF"
else
    cp "$SRC_CONF" "$CONF"
    if grep -q "在这里填你的学号" "$CONF" 2>/dev/null; then
        echo "    [!] 已安装模板 $CONF，但账号密码还没填！请立即编辑:"
        echo "        vi $CONF"
        echo "    填好后重新执行: sh $SERVICE start"
    else
        echo "    已安装 $CONF"
    fi
fi

echo "==> 4/5 写入开机自启与守护"
AUTOSTART_LINE="sh $SERVICE start"

# 开机自启: /etc/rc.local（原厂 MiWiFi 保留此文件）
if [ ! -f /etc/rc.local ]; then
    printf '#!/bin/sh\n%s\nexit 0\n' "$AUTOSTART_LINE" > /etc/rc.local
    chmod +x /etc/rc.local
    echo "    已创建 /etc/rc.local"
elif grep -q "drcom-service.sh" /etc/rc.local 2>/dev/null; then
    echo "    /etc/rc.local 中已存在自启项，跳过"
elif grep -q "^exit 0" /etc/rc.local 2>/dev/null; then
    # 插到 exit 0 之前
    sed -i "/^exit 0/i $AUTOSTART_LINE" /etc/rc.local
    echo "    已插入 /etc/rc.local"
else
    # 没有 exit 0，直接追加
    printf '%s\n' "$AUTOSTART_LINE" >> /etc/rc.local
    echo "    已追加到 /etc/rc.local"
fi

# 守护进程: crontab 每分钟检查，挂了自动拉起
if command -v crontab >/dev/null 2>&1; then
    ( crontab -l 2>/dev/null | grep -v "drcom-service.sh" ; \
      echo "* * * * * sh $SERVICE start >/dev/null 2>&1" ) | crontab -
    echo "    已添加 crontab 守护（每分钟检查一次）"
else
    echo "    未找到 crontab，跳过守护（仅靠 rc.local 开机自启）"
fi

echo "==> 5/5 启动客户端"
"$SERVICE" start

echo ""
echo "安装完成。常用命令:"
echo "  查看状态:  sh $SERVICE status"
echo "  查看日志:  tail -f /data/drcom/drcom.log"
echo "  停止:      sh $SERVICE stop"
echo ""
echo "提示: 确认 WAN 口已按学校分配配好静态 IP/掩码/网关后，日志里出现『登录成功』即大功告成。"

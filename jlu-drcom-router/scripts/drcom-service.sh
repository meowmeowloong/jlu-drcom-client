#!/bin/sh
# DrCOM 客户端服务脚本（在路由器上运行）
# 用法: sh drcom-service.sh {start|stop|restart|status}
#
# 可调整的路径（一般不用改）:
BIN=/data/drcom/drcom
CONF=/data/drcom/drcom.conf
PIDFILE=/var/run/drcom.pid
LOGFILE=/data/drcom/drcom.log

start() {
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo "DrCOM 已在运行 (pid $(cat "$PIDFILE"))"
        return 0
    fi
    if [ ! -x "$BIN" ]; then
        echo "找不到可执行文件 $BIN"
        return 1
    fi
    echo "启动 DrCOM 客户端..."
    # 前台运行，重定向日志，后台化
    "$BIN" "$CONF" >> "$LOGFILE" 2>&1 &
    echo $! > "$PIDFILE"
    echo "已启动 (pid $(cat "$PIDFILE"))，日志: $LOGFILE"
}

stop() {
    if [ -f "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE")
        if kill -0 "$PID" 2>/dev/null; then
            kill "$PID"
            echo "已停止 (pid $PID)"
        fi
        rm -f "$PIDFILE"
    else
        # 兜底：按名字杀
        killall drcom 2>/dev/null && echo "已停止 (killall)" || echo "未在运行"
    fi
}

status() {
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo "运行中 (pid $(cat "$PIDFILE"))"
        return 0
    fi
    echo "未运行"
    return 1
}

case "$1" in
    start)    start ;;
    stop)     stop ;;
    restart)  stop; sleep 1; start ;;
    status)   status ;;
    *)        echo "用法: $0 {start|stop|restart|status}"; exit 1 ;;
esac

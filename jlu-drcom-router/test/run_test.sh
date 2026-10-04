#!/bin/sh
# 本地协议一致性测试：用假服务器陪客户端跑完整流程，并逐字节校验登录包。
#
# 用法（在 Linux / WSL 里）:
#   sh test/run_test.sh
#
# 原理: test/fake_server.py 里有一份独立的 Python 参考实现，
#       它按 C++ 客户端算法重建"期望的登录包"，与 C 客户端实际发出的包比对。
set -e

cd "$(dirname "$0")/.."
BIN=/tmp/jlu-drcom-test-x86

echo "==> 编译本机测试版客户端"
gcc -O2 -Wall -Wextra -o "$BIN" src/jlu-drcom.c src/md5.c src/md4.c src/sha1.c
echo "    OK: $BIN"
echo

# 各用例: 名称 用户名 密码 MAC 端口
run_case() {
    NAME="$1"; USER="$2"; PASS="$3"; MAC="$4"; PORT="$5"
    sed -e "s/^username .*/username  $USER/" \
        -e "s/^password .*/password  $PASS/" \
        -e "s/^mac .*/mac       $MAC/" \
        -e "s/^port .*/port      $PORT/" \
        test/drcom-test.conf > /tmp/drcom-case.conf

    DRCOM_USER="$USER" DRCOM_PASS="$PASS" DRCOM_MAC="$MAC" PYTHONUNBUFFERED=1 \
        python3 test/fake_server.py "$PORT" > "/tmp/drcom-srv-$PORT.log" 2>&1 &
    SRV=$!
    sleep 2
    timeout 20 "$BIN" /tmp/drcom-case.conf >/dev/null 2>&1 || true
    sleep 1
    kill $SRV 2>/dev/null || true

    echo "----- 用例: $NAME -----"
    if grep -q "逐字节校验通过" "/tmp/drcom-srv-$PORT.log"; then
        echo "  ✓ 通过"
    else
        echo "  ✗ 失败"
        cat "/tmp/drcom-srv-$PORT.log"
        FAILED=1
    fi
}

FAILED=0
run_case "8位密码"        testuser test1234          001122334455 61442
run_case "10位密码(JLU)"  testuser test123456        001122334455 61443
run_case "16位密码(JLU)"  testuser test123456789012  001122334455 61444
run_case "无MAC校验"      testuser test1234          000000000000 61445

echo
if [ "$FAILED" = "0" ]; then
    echo "全部用例通过 ✓"
else
    echo "存在失败用例 ✗"
    exit 1
fi

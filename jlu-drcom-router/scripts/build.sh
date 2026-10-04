#!/bin/sh
# 交叉编译脚本：编译出红米 AX3000 (RA81) 可用的静态二进制。
#
# 产物: bin/jlu-drcom-armv7   （完全静态、软浮点，无需路由器上有任何库，直接能跑）
#
# ---------------------------------------------------------------------------
# 为什么是 armv7 而不是 aarch64？（实测结论，别改错）
# ---------------------------------------------------------------------------
# 红米 AX3000 的 SoC 是高通 IPQ5018（CPU 本身是 64 位的 Cortex-A53），
# 但小米原厂固件的**用户态是 32 位 ARMv7**：
#     # uname -m            -> armv7l
#     # cat /etc/openwrt_release | grep ARCH -> arm_cortex-a7
#     # readelf -h /usr/sbin/crond -> ELF32, ARM, Version5 EABI, soft-float ABI
# 所以在上面跑 aarch64 二进制会直接 "Exec format error"。
# 必须编 32 位 ARM、且是 soft-float ABI（路由器自带二进制就是软浮点）。
#
# ---------------------------------------------------------------------------
# 怎么用（编译必须在 Linux 下跑 —— Windows 的 Git Bash 执行不了 Linux 工具链）
# ---------------------------------------------------------------------------
# 方式 A（推荐，无需 sudo）：在 WSL / Linux 里下载 musl 交叉工具链
#   curl -O https://musl.cc/arm-linux-musleabi-cross.tgz
#   tar xzf arm-linux-musleabi-cross.tgz
#   export PATH=$HOME/arm-linux-musleabi-cross/bin:$PATH
#   sh scripts/build.sh
#
# 方式 B：用系统包管理器安装（需要 sudo 密码）
#   sudo apt-get install -y gcc-arm-linux-gnueabi
#   sh scripts/build.sh
#
# 想编 64 位版（其他 aarch64 路由器）：
#   ARCH=aarch64 sh scripts/build.sh
# ---------------------------------------------------------------------------
set -e

cd "$(dirname "$0")/.."
mkdir -p bin

ARCH="${ARCH:-armv7}"

case "$ARCH" in
    armv7)
        OUT=bin/jlu-drcom-armv7
        # 依次尝试可用的 armv7 交叉编译器（优先 musl 软浮点）
        if command -v arm-linux-musleabi-gcc >/dev/null 2>&1; then
            CC=arm-linux-musleabi-gcc
        elif [ -x "$HOME/arm-linux-musleabi-cross/bin/arm-linux-musleabi-gcc" ]; then
            CC="$HOME/arm-linux-musleabi-cross/bin/arm-linux-musleabi-gcc"
        elif command -v arm-linux-gnueabi-gcc >/dev/null 2>&1; then
            CC=arm-linux-gnueabi-gcc
        else
            echo "错误：找不到 armv7 交叉编译器，请先按脚本头部注释安装。" >&2
            exit 1
        fi
        # -march=armv7-a：明确目标指令集；软浮点由编译器自带（musleabi / gnueabi 均为软浮点）
        ARCH_FLAGS="-march=armv7-a"
        ;;
    aarch64)
        OUT=bin/jlu-drcom-aarch64
        if command -v aarch64-linux-musl-gcc >/dev/null 2>&1; then
            CC=aarch64-linux-musl-gcc
        elif [ -x "$HOME/aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc" ]; then
            CC="$HOME/aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc"
        elif command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
            CC=aarch64-linux-gnu-gcc
        else
            echo "错误：找不到 aarch64 交叉编译器。" >&2
            exit 1
        fi
        ARCH_FLAGS=""
        ;;
    *)
        echo "错误：不支持的 ARCH=$ARCH（可选 armv7 / aarch64）" >&2
        exit 1
        ;;
esac

echo "目标架构: $ARCH"
echo "交叉编译器: $CC"

# -static -no-pie：完全静态、非 PIE，兼容老内核（原厂 MiWiFi 基于 OpenWrt 18.06 / Linux 4.4）
# shellcheck disable=SC2086
$CC $ARCH_FLAGS -O2 -static -no-pie -Wall -Wextra \
    -o "$OUT" \
    src/jlu-drcom.c src/md5.c src/md4.c src/sha1.c

# 去掉调试信息，体积减半
STRIP="${CC%gcc}strip"
if [ -x "$STRIP" ]; then
    "$STRIP" "$OUT" 2>/dev/null || true
fi

echo "编译完成: $OUT"
ls -l "$OUT"

# 顺手打印 ELF 头，确认架构/ABI 没编错
if command -v readelf >/dev/null 2>&1; then
    echo "--- ELF 校验 ---"
    readelf -h "$OUT" | grep -E "Class|Machine|Flags|Type"
fi

#!/usr/bin/env python3
"""
本地假 DrCOM 认证服务器 + 独立参考实现，用于验证 jlu-drcom.c 的协议实现是否正确。

它做两件事：
  1. 扮演认证服务器，陪客户端走完 challenge -> login -> keepalive 全流程；
  2. 用纯 Python 独立实现同一套算法，把客户端实际发出的登录包与"期望值"逐字节比对。

用法:
    python3 fake_server.py <port>

然后在另一个终端用同一端口运行客户端（配置文件里写 port = <port>）。
"""

import hashlib
import socket
import struct
import sys
import threading
import time

# ---- 测试输入（可用环境变量覆盖，以便测试不同密码长度/分支）----
import os
USERNAME = os.environ.get("DRCOM_USER", "testuser")
PASSWORD = os.environ.get("DRCOM_PASS", "test1234")
MAC = bytes.fromhex(os.environ.get("DRCOM_MAC", "001122334455"))
SEED = bytes([0x01, 0x02, 0x03, 0x04])   # challenge 下发的 salt
SERVER_IP = bytes([10, 100, 61, 3])


def ror(md5a: bytes, pwd: bytes) -> bytes:
    """与 C++ 客户端一致的 ror 实现"""
    ret = bytearray()
    for i in range(len(pwd)):
        x = md5a[i] ^ pwd[i]
        ret.append(((x << 3) & 0xFF) + (x >> 5))
    return bytes(ret)


def build_reference_login_packet() -> bytes:
    """
    按 jlu-drcom-win-QT/src/dogcom.cpp 的算法，独立重建"期望的"登录包。
    这段是与 C 实现相互独立的第二实现，用于交叉验证。
    """
    pw = PASSWORD.encode()
    un = USERNAME.encode()
    pwlen = len(pw)

    # 计算包大小 / padding（与 C++ 完全一致）
    length_padding = 0
    jlu_padding = 0
    if pwlen > 8:
        length_padding = pwlen - 8 + (length_padding % 2)
        if pwlen != 16:
            jlu_padding = pwlen // 4
        length_padding = 28 + pwlen - 8 + jlu_padding
    login_packet_size = 338 + length_padding

    pkt = bytearray(login_packet_size)

    # --- 头部 ---
    pkt[0] = 0x03
    pkt[1] = 0x01
    pkt[2] = 0x00
    pkt[3] = len(un) + 20

    # MD5A = MD5(03 01 + seed + password)
    md5a = hashlib.md5(bytes([0x03, 0x01]) + SEED + pw).digest()
    pkt[4:20] = md5a

    pkt[20:20 + len(un)] = un
    pkt[56] = 0x20          # CONTROLCHECKSTATUS
    pkt[57] = 0x03          # ADAPTERNUM

    # MAC xor MD5A[0:6]（双方都按大端 48 位整数异或）
    sum_md5a = int.from_bytes(md5a[:6], "big")
    sum_mac = int.from_bytes(MAC, "big")
    pkt[58:64] = ((sum_md5a ^ sum_mac) & 0xFFFFFFFFFFFF).to_bytes(6, "big")

    # MD5B = MD5(01 + password + seed + 0000)
    md5b = hashlib.md5(bytes([0x01]) + pw + SEED + b"\x00" * 4).digest()
    pkt[64:80] = md5b

    pkt[80] = 0x01
    # [81:85] host_ip = 0.0.0.0

    # checksum1 = MD5(pkt[0:97] + 14 00 07 0b)
    checksum1 = hashlib.md5(bytes(pkt[0:97]) + bytes([0x14, 0x00, 0x07, 0x0B])).digest()
    pkt[97:105] = checksum1[:8]

    pkt[105] = 0x01         # IPDOG
    pkt[110:120] = b"DrCOM-JLU "
    pkt[142:146] = bytes([10, 10, 10, 10])   # PRIMARY_DNS
    # [146:150] dhcp_server = 0

    pkt[162:166] = bytes([0x94, 0, 0, 0])
    pkt[166:170] = bytes([0x06, 0, 0, 0])
    pkt[170:174] = bytes([0x02, 0, 0, 0])
    pkt[174:178] = bytes([0xF0, 0x23, 0, 0])
    pkt[178:182] = bytes([0x02, 0, 0, 0])

    pkt[182:191] = bytes([0x44, 0x72, 0x43, 0x4F, 0x4D, 0x00, 0xCF, 0x07, 0x68])

    service_pack = b"3dc79f5212e8170acfa9ec95f1d74916542be7b1"
    pkt[246:246 + len(service_pack)] = service_pack

    pkt[310] = 0x68
    pkt[311] = 0x00

    # --- ror 段 ---
    counter = 312
    if pwlen <= 8:
        ror_padding = 8 - pwlen
    else:
        ror_padding = jlu_padding

    pkt[counter + 1] = pwlen
    counter += 2
    ror_bytes = ror(md5a, pw)
    pkt[counter:counter + pwlen] = ror_bytes
    counter += pwlen

    pkt[counter] = 0x02
    pkt[counter + 1] = 0x0C

    # --- checksum2 ---
    checksum2_str = bytearray(counter + 18)
    checksum2_str[0:counter + 2] = pkt[0:counter + 2]
    checksum2_str[counter + 2:counter + 8] = bytes([0x01, 0x26, 0x07, 0x11, 0x00, 0x00])
    checksum2_str[counter + 8:counter + 14] = MAC

    total = 1234
    for i in range(0, counter + 14, 4):
        val = int.from_bytes(checksum2_str[i:i + 4], "little")
        total ^= val
    total = (1968 * total) & 0xFFFFFFFF
    checksum2 = total.to_bytes(4, "little")

    pkt[counter + 2:counter + 6] = checksum2
    pkt[counter + 8:counter + 14] = MAC
    pkt[counter + ror_padding + 14] = 0x60
    pkt[counter + ror_padding + 15] = 0xA2

    return bytes(pkt)


def hexdump(data: bytes) -> str:
    return " ".join(f"{b:02x}" for b in data)


def diff_report(actual: bytes, expected: bytes) -> bool:
    """比对两个包，打印差异"""
    if len(actual) != len(expected):
        print(f"  ✗ 长度不同: 实际 {len(actual)}, 期望 {len(expected)}")
    n = min(len(actual), len(expected))
    diffs = [i for i in range(n) if actual[i] != expected[i]]
    if len(actual) == len(expected) and not diffs:
        print(f"  ✓ 完全一致（{len(actual)} 字节）")
        return True
    print(f"  ✗ 共 {len(diffs)} 处字节不同:")
    for i in diffs[:20]:
        print(f"     offset {i:4d}: 实际 {actual[i]:02x}  期望 {expected[i]:02x}")
    return False


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 61441
    expected_login = build_reference_login_packet()

    print(f"=== 假 DrCOM 服务器启动，端口 {port} ===")
    print(f"账号={USERNAME} 密码={PASSWORD} MAC={MAC.hex()}")
    print(f"参考实现生成的登录包长度: {len(expected_login)}")
    print()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.2", port))
    sock.settimeout(15)

    results = {"login_match": None, "flows": []}

    try:
        while True:
            data, addr = sock.recvfrom(4096)
            if not data:
                continue
            tag = data[0]

            # ---- challenge (0x01) ----
            if tag == 0x01 and len(data) == 20:
                print(f"[收到] challenge ({len(data)} 字节) rand={data[2]:02x}{data[3]:02x}")
                resp = bytearray(76)
                resp[0] = 0x02
                resp[1] = data[1]
                resp[2] = data[2]
                resp[3] = data[3]
                resp[4:8] = SEED
                resp[20:24] = bytes([192, 168, 1, 100])   # 分配给客户端的 IP
                sock.sendto(bytes(resp), addr)
                print(f"[发送] challenge 响应 salt={SEED.hex()}")
                results["flows"].append("challenge")

            # ---- login (0x03) ----
            elif tag == 0x03:
                print(f"[收到] login ({len(data)} 字节)")
                ok = diff_report(data, expected_login)
                results["login_match"] = ok
                if ok:
                    print("  >>> 登录包逐字节校验通过 <<<")
                else:
                    print("  >>> 登录包存在差异，实现有 BUG <<<")

                # 回复登录成功
                resp = bytearray(40)
                resp[0] = 0x04
                resp[23:39] = bytes(range(16))   # auth_information (tail1)
                sock.sendto(bytes(resp), addr)
                print("[发送] login 成功响应 (0x04)")
                results["flows"].append("login")

            # ---- keepalive_1 挑战 (0x07 01) ----
            elif tag == 0x07 and len(data) == 8 and data[1] == 0x01:
                print(f"[收到] keepalive_1 挑战 ({len(data)} 字节)")
                resp = bytearray(40)
                resp[0] = 0x07
                resp[1] = 0x01
                resp[2] = 0x28
                resp[8:12] = bytes([0x01, 0x02, 0x03, 0x04])  # keepalive seed
                sock.sendto(bytes(resp), addr)
                print("[发送] keepalive_1 挑战响应")
                results["flows"].append("keepalive1_challenge")

            # ---- keepalive_1 数据包 (0xff) ----
            elif tag == 0xFF:
                print(f"[收到] keepalive_1 数据包 ({len(data)} 字节)")
                resp = bytearray(40)
                resp[0] = 0x07
                sock.sendto(bytes(resp), addr)
                print("[发送] keepalive_1 响应")
                results["flows"].append("keepalive1_data")

            # ---- keepalive_2 (0x07 ...) ----
            elif tag == 0x07 and len(data) == 40:
                ptype = data[5]
                print(f"[收到] keepalive_2 包 type={ptype} counter={data[1]}")
                resp = bytearray(40)
                resp[0] = 0x07
                resp[1] = data[1]
                resp[2] = 0x10 if ptype == 1 and data[6] == 0x0F else 0x28
                resp[16:20] = bytes([0xAA, 0xBB, 0xCC, 0xDD])
                sock.sendto(bytes(resp), addr)
                results["flows"].append(f"keepalive2_type{ptype}")

                # 走完一轮就总结退出
                if results["flows"].count("keepalive2_type3") >= 1:
                    raise KeyboardInterrupt

            else:
                print(f"[收到] 未知包 type=0x{tag:02x} len={len(data)}")

    except (socket.timeout, KeyboardInterrupt):
        pass
    finally:
        sock.close()

    print()
    print("=" * 50)
    print("测试结果")
    print("=" * 50)
    print(f"流程覆盖: {', '.join(results['flows']) or '（无）'}")
    if results["login_match"] is True:
        print("✓ 登录包逐字节一致 —— 协议实现正确")
    elif results["login_match"] is False:
        print("✗ 登录包有差异 —— 实现存在问题")
    else:
        print("? 未收到登录包")
    return 0 if results["login_match"] else 1


if __name__ == "__main__":
    sys.exit(main())

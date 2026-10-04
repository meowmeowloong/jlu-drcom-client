/*
 * jlu-drcom.c — JLU (吉林大学) DrCOM 校园网认证客户端（路由器版）
 *
 * 这是一个把本仓库已知可用的 C++ dogcom 客户端（jlu-drcom-win-QT / jlu-drcom-win32）
 * 忠实地移植到 POSIX/Linux 的版本，专为运行在红米/小米路由器（原厂 MiWiFi，经 SSH 解锁）
 * 这类没有 Python、只能跑静态二进制的小设备上而写。
 *
 * 协议细节严格对齐项目里确认可用的客户端：
 *   - 认证服务器默认 10.100.61.3（JLU DrCOM 5.2，AUTH_VERSION = 0x68 00）
 *   - challenge -> login -> keepalive_1 -> keepalive_2 循环
 *   - 断线自动重新登录
 *
 * 仅依赖 libc，无任何第三方库，可用 musl/glibc 静态编译。
 *
 * 用法:
 *   jlu-drcom [配置文件路径]      # 默认读取 /etc/drcom.conf
 *
 * 配置文件格式（key = value，支持 # 注释）:
 *   server    10.100.61.3
 *   username  你的学号
 *   password  你的密码
 *   mac       11:22:33:44:55:66   # 网络中心绑定的 MAC（吉大必填，同时 WAN 口要克隆成它）
 *   host_name JLU-DrCOM           # 可选，主机名
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "md5.h"
#include "md4.h"
#include "sha1.h"

/* 认证端口，JLU 固定为 61440。做成可配置仅为方便本地测试 */
static int drcom_port = 61440;

/* JLU 协议常量（与已确认可用的 C++ 客户端一致） */
#define AUTH_VERSION_HI    0x68
#define AUTH_VERSION_LO    0x00
#define KEEP_ALIVE_VER_HI  0xdc
#define KEEP_ALIVE_VER_LO  0x02
#define CONTROL_CHECK      0x20
#define ADAPTER_NUM        0x03
#define IPDOG              0x01

/* 返回码 */
#define LOGIN_OK            0
#define LOGIN_ERR_UNKNOWN  -1
#define LOGIN_ERR_TIMEOUT -2

/* 全局配置 */
static char server[64]     = "10.100.61.3";
static char username[64]   = "";
static char password[64]   = "";
static char host_name[64]  = "JLU-DrCOM";
static char local_ip[64]   = "";   /* 空 = 绑定 INADDR_ANY；一般无需设置 */
static unsigned char mac[6] = {0, 0, 0, 0, 0, 0};  /* 全 0 = 不校验 MAC */

static int debug = 0;      /* 由配置文件 debug=1 开启，打印每个报文 */

/* ------------------------------------------------------------------ */
/* 日志                                                            */
/* ------------------------------------------------------------------ */

static void logline(const char *level, const char *fmt, ...) {
    time_t now = time(NULL);
    char ts[32];
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
    fprintf(stderr, "[%s] [%s] ", ts, level);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    fflush(stderr);
}

static void print_packet(const char *msg, const unsigned char *packet, int length) {
    if (!debug) return;
    char buf[2048];
    int off = 0;
    off += snprintf(buf + off, sizeof(buf) - off, "%s", msg);
    for (int i = 0; i < length && off < (int)sizeof(buf) - 4; i++)
        off += snprintf(buf + off, sizeof(buf) - off, "%02x ", packet[i]);
    fprintf(stderr, "%s\n", buf);
}

/* ------------------------------------------------------------------ */
/* 配置解析                                                          */
/* ------------------------------------------------------------------ */

static void trim(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    size_t len = strlen(p);
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' ||
                       p[len - 1] == '\r' || p[len - 1] == '\n'))
        p[--len] = '\0';
    memmove(s, p, len + 1);
}

/* 解析 MAC: 支持 "aa:bb:cc:dd:ee:ff"、"aabbccddeeff"；"0"/"none"/"default" -> 全 0 */
static void parse_mac(const char *str, unsigned char out[6]) {
    memset(out, 0, 6);
    if (!str || !str[0] || strcmp(str, "0") == 0 ||
        strcasecmp(str, "none") == 0 || strcasecmp(str, "default") == 0)
        return;

    /* 去掉冒号/横杠 */
    char hex[16] = {0};
    int n = 0;
    for (const char *p = str; *p; p++) {
        if (*p == ':' || *p == '-' || *p == ' ') continue;
        if (n < 12) hex[n++] = *p;
    }
    hex[n] = '\0';
    if (n < 12) {
        logline("WARN", "MAC 长度不足 12 位(%d)，已按 0 处理: %s", n, str);
        memset(out, 0, 6);
        return;
    }
    for (int i = 0; i < 6; i++) {
        unsigned int b;
        sscanf(hex + 2 * i, "%2x", &b);
        out[i] = (unsigned char)b;
    }
}

static int parse_config(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        logline("ERROR", "无法打开配置文件 %s: %s", path, strerror(errno));
        return -1;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (!line[0] || line[0] == '#') continue;

        /* 找到 key / value 分隔符 */
        char *sep = strchr(line, '=');
        char *key = line, *val;
        if (sep) {
            *sep = '\0';
            val = sep + 1;
        } else {
            /* 允许 "key value" 空格分隔 */
            char *sp = strpbrk(line, " \t");
            if (!sp) continue;
            *sp = '\0';
            val = sp + 1;
        }
        trim(key);
        trim(val);

        /* 去掉值两端的引号 */
        size_t vl = strlen(val);
        if (vl >= 2 && ((val[0] == '"' && val[vl - 1] == '"') ||
                        (val[0] == '\'' && val[vl - 1] == '\''))) {
            val[vl - 1] = '\0';
            val++;
        }

        if (strcmp(key, "server") == 0) {
            snprintf(server, sizeof(server), "%s", val);
        } else if (strcmp(key, "username") == 0) {
            snprintf(username, sizeof(username), "%s", val);
        } else if (strcmp(key, "password") == 0) {
            snprintf(password, sizeof(password), "%s", val);
        } else if (strcmp(key, "mac") == 0) {
            parse_mac(val, mac);
        } else if (strcmp(key, "host_name") == 0 || strcmp(key, "hostname") == 0) {
            snprintf(host_name, sizeof(host_name), "%s", val);
        } else if (strcmp(key, "debug") == 0) {
            debug = atoi(val);
        } else if (strcmp(key, "port") == 0) {
            drcom_port = atoi(val);
        } else if (strcmp(key, "local_ip") == 0) {
            snprintf(local_ip, sizeof(local_ip), "%s", val);
        }
    }
    fclose(f);

    if (!username[0] || !password[0]) {
        logline("ERROR", "配置缺少 username/password");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 协议实现（与 C++ 客户端逐字节一致）                                */
/* ------------------------------------------------------------------ */

static void gen_crc(const unsigned char seed[4], int encrypt_type, unsigned char crc[8]) {
    if (encrypt_type == 0) {
        unsigned char DRCOM_DIAL_EXT_PROTO_CRC_INIT[4] = {0xc7, 0x2f, 0x31, 0x01};
        unsigned char gencrc_tmp[4] = {0x7e, 0, 0, 0};
        memcpy(crc, DRCOM_DIAL_EXT_PROTO_CRC_INIT, 4);
        memcpy(crc + 4, gencrc_tmp, 4);
    } else if (encrypt_type == 1) {
        unsigned char hash[16];
        MD5(seed, 4, hash);
        crc[0] = hash[2]; crc[1] = hash[3]; crc[2] = hash[8]; crc[3] = hash[9];
        crc[4] = hash[5]; crc[5] = hash[6]; crc[6] = hash[13]; crc[7] = hash[14];
    } else if (encrypt_type == 2) {
        unsigned char hash[16];
        MD4(seed, 4, hash);
        crc[0] = hash[1]; crc[1] = hash[2]; crc[2] = hash[8]; crc[3] = hash[9];
        crc[4] = hash[4]; crc[5] = hash[5]; crc[6] = hash[11]; crc[7] = hash[12];
    } else { /* encrypt_type == 3 */
        unsigned char hash[20];
        SHA1(seed, 4, hash);
        crc[0] = hash[2]; crc[1] = hash[3]; crc[2] = hash[9]; crc[3] = hash[10];
        crc[4] = hash[5]; crc[5] = hash[6]; crc[6] = hash[15]; crc[7] = hash[16];
    }
}

/* challenge: 返回 0 成功（seed 由服务器下发），非 0 失败 */
static int dhcp_challenge(int sockfd, struct sockaddr_in *addr, unsigned char seed[4]) {
    unsigned char challenge_packet[20], recv_packet[1024];
    memset(challenge_packet, 0, 20);
    challenge_packet[0] = 0x01;
    challenge_packet[1] = 0x02;
    challenge_packet[2] = rand() & 0xff;
    challenge_packet[3] = rand() & 0xff;
    challenge_packet[4] = AUTH_VERSION_HI;

    int tries = 0;
    for (;;) {
        if (sendto(sockfd, (const char *)challenge_packet, 20, 0,
                   (struct sockaddr *)addr, sizeof(*addr)) < 0) {
            logline("ERROR", "challenge send 失败: %s", strerror(errno));
            return -1;
        }
        print_packet("[Challenge sent]", challenge_packet, 20);

        socklen_t addrlen = sizeof(*addr);
        int n = recvfrom(sockfd, (char *)recv_packet, sizeof(recv_packet), 0,
                         (struct sockaddr *)addr, &addrlen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (++tries >= 5) {
                    logline("ERROR", "challenge 多次超时，服务器无响应");
                    return -1;
                }
                logline("WARN", "challenge 超时，重试 %d/5 ...", tries);
                continue;
            }
            logline("ERROR", "challenge recv 失败: %s", strerror(errno));
            return -1;
        }
        break;
    }

    print_packet("[Challenge recv]", recv_packet, 76);
    if (recv_packet[0] != 0x02) {
        logline("ERROR", "challenge 响应异常");
        return -1;
    }
    memcpy(seed, &recv_packet[4], 4);
    return 0;
}

/* login: 返回 0 成功（auth_information 用于 keepalive），
 * 否则返回服务器错误码（recv[4]）或负值。 */
static int dhcp_login(int sockfd, struct sockaddr_in *addr,
                      const unsigned char seed[4], unsigned char auth_information[16]) {
    int pwLen = (int)strlen(password);
    /* 协议规定密码最长 16 字节；超过会产生越界访问，这里强制截断 */
    if (pwLen > 16) {
        logline("WARN", "密码超过 16 字节，按协议截断为 16 字节");
        pwLen = 16;
    }
    unsigned int length_padding = 0;
    int JLU_padding = 0;

    if (pwLen > 8) {
        length_padding = pwLen - 8 + (length_padding % 2);
        if (pwLen != 16)
            JLU_padding = pwLen / 4;
        length_padding = 28 + pwLen - 8 + JLU_padding;
    }
    int login_packet_size = 338 + (int)length_padding;

    unsigned char *login_packet = calloc(1, login_packet_size);
    unsigned char recv_packet[1024];
    unsigned char MD5A[16], MACxorMD5A[6], MD5B[16], checksum1[16], checksum2[4];
    if (!login_packet) {
        logline("ERROR", "内存分配失败");
        return LOGIN_ERR_UNKNOWN;
    }
    memset(recv_packet, 0, 100);

    int unameLen = (int)strlen(username);

    /* --- 组装登录包 --- */
    login_packet[0] = 0x03;
    login_packet[1] = 0x01;
    login_packet[2] = 0x00;
    login_packet[3] = (unsigned char)(unameLen + 20);

    /* MD5A = MD5(03 01 + salt(4) + password) */
    int MD5A_len = 6 + pwLen;
    unsigned char *MD5A_str = malloc(MD5A_len);
    MD5A_str[0] = 0x03;
    MD5A_str[1] = 0x01;
    memcpy(MD5A_str + 2, seed, 4);
    memcpy(MD5A_str + 6, password, pwLen);
    MD5(MD5A_str, MD5A_len, MD5A);
    memcpy(login_packet + 4, MD5A, 16);

    memcpy(login_packet + 20, username, unameLen);
    login_packet[56] = CONTROL_CHECK;
    login_packet[57] = ADAPTER_NUM;

    /* MACxorMD5A = (MD5A[0..5] 作为大端 48 位) ^ mac */
    uint64_t sum = 0, macv = 0;
    for (int i = 0; i < 6; i++) sum = (uint64_t)MD5A[i] + sum * 256;
    for (int i = 0; i < 6; i++) macv = (uint64_t)mac[i] + macv * 256;
    sum ^= macv;
    for (int i = 6; i > 0; i--) {
        MACxorMD5A[i - 1] = (unsigned char)(sum % 256);
        sum /= 256;
    }
    memcpy(login_packet + 58, MACxorMD5A, 6);

    /* MD5B = MD5(01 + password + salt(4) + 0000) */
    int MD5B_len = 9 + pwLen;
    unsigned char *MD5B_str = calloc(1, MD5B_len);
    MD5B_str[0] = 0x01;
    memcpy(MD5B_str + 1, password, pwLen);
    memcpy(MD5B_str + pwLen + 1, seed, 4);
    MD5(MD5B_str, MD5B_len, MD5B);
    memcpy(login_packet + 64, MD5B, 16);

    login_packet[80] = 0x01;
    /* [81..84] host_ip = 0.0.0.0（与可用客户端一致） */
    unsigned char checksum1_str[101], checksum1_tmp[4] = {0x14, 0x00, 0x07, 0x0b};
    memcpy(checksum1_str, login_packet, 97);
    memcpy(checksum1_str + 97, checksum1_tmp, 4);
    MD5(checksum1_str, 101, checksum1);
    memcpy(login_packet + 97, checksum1, 8);

    login_packet[105] = IPDOG;
    {
        size_t hn_len = strlen(host_name);
        if (hn_len > 32) hn_len = 32;   /* 避免覆盖后面的 OS 字段 */
        memcpy(login_packet + 110, host_name, hn_len);
    }

    unsigned char PRIMARY_DNS[4] = {10, 10, 10, 10};
    memcpy(login_packet + 142, PRIMARY_DNS, 4);
    /* [146..149] dhcp_server = 0.0.0.0（calloc 已清零） */

    unsigned char OSVersionInfoSize[4] = {0x94, 0, 0, 0};
    unsigned char OSMajor[4]            = {0x06, 0, 0, 0};
    unsigned char OSMinor[4]            = {0x02, 0, 0, 0};
    unsigned char OSBuild[4]            = {0xf0, 0x23, 0, 0};
    unsigned char PlatformID[4]         = {0x02, 0, 0, 0};
    /* ServicePack: JLU 特有的字符串 "3dc79f5212e8170acfa9ec95f1d74916542be7b1" */
    unsigned char ServicePack[40] = {
        0x33, 0x64, 0x63, 0x37, 0x39, 0x66, 0x35, 0x32, 0x31, 0x32,
        0x65, 0x38, 0x31, 0x37, 0x30, 0x61, 0x63, 0x66, 0x61, 0x39,
        0x65, 0x63, 0x39, 0x35, 0x66, 0x31, 0x64, 0x37, 0x34, 0x39,
        0x31, 0x36, 0x35, 0x34, 0x32, 0x62, 0x65, 0x37, 0x62, 0x31
    };
    unsigned char hostname[9] = {0x44, 0x72, 0x43, 0x4f, 0x4d, 0x00, 0xcf, 0x07, 0x68};

    memcpy(login_packet + 182, hostname, 9);
    memcpy(login_packet + 246, ServicePack, 40);
    memcpy(login_packet + 162, OSVersionInfoSize, 4);
    memcpy(login_packet + 166, OSMajor, 4);
    memcpy(login_packet + 170, OSMinor, 4);
    memcpy(login_packet + 174, OSBuild, 4);
    memcpy(login_packet + 178, PlatformID, 4);

    login_packet[310] = AUTH_VERSION_HI;
    login_packet[311] = AUTH_VERSION_LO;

    int counter = 312;
    unsigned int ror_padding = 0;
    if (pwLen <= 8)
        ror_padding = 8 - pwLen;
    else
        ror_padding = JLU_padding;

    MD5(MD5A_str, MD5A_len, MD5A);  /* 原版这里重复计算了一次 MD5A（结果相同） */
    login_packet[counter + 1] = (unsigned char)pwLen;
    counter += 2;
    for (int i = 0, x = 0; i < pwLen; i++) {
        x = (int)MD5A[i] ^ (int)password[i];
        login_packet[counter + i] = (unsigned char)(((x << 3) & 0xff) + (x >> 5));
    }
    counter += pwLen;

    login_packet[counter]     = 0x02;
    login_packet[counter + 1] = 0x0c;

    unsigned char *checksum2_str = calloc(1, counter + 18);
    unsigned char checksum2_tmp[6] = {0x01, 0x26, 0x07, 0x11, 0x00, 0x00};
    memcpy(checksum2_str, login_packet, counter + 2);
    memcpy(checksum2_str + counter + 2, checksum2_tmp, 6);
    memcpy(checksum2_str + counter + 8, mac, 6);

    sum = 1234;
    uint64_t ret = 0;
    for (int i = 0; i < counter + 14; i += 4) {
        ret = 0;
        for (int j = 4; j > 0; j--)
            ret = ret * 256 + (uint64_t)checksum2_str[i + j - 1];
        sum ^= ret;
    }
    sum = (1968 * sum) & 0xffffffffULL;
    for (int j = 0; j < 4; j++)
        checksum2[j] = (unsigned char)((sum >> (j * 8)) & 0xff);

    memcpy(login_packet + counter + 2, checksum2, 4);
    memcpy(login_packet + counter + 8, mac, 6);
    login_packet[counter + ror_padding + 14] = 0x60;
    login_packet[counter + ror_padding + 15] = 0xa2;

    free(MD5A_str);
    free(MD5B_str);
    free(checksum2_str);

    /* --- 发送并接收 --- */
    if (sendto(sockfd, (const char *)login_packet, login_packet_size, 0,
               (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        logline("ERROR", "login send 失败: %s", strerror(errno));
        free(login_packet);
        return LOGIN_ERR_UNKNOWN;
    }
    print_packet("[Login sent]", login_packet, login_packet_size);
    free(login_packet);

    socklen_t addrlen = sizeof(*addr);
    int n = recvfrom(sockfd, (char *)recv_packet, sizeof(recv_packet), 0,
                     (struct sockaddr *)addr, &addrlen);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            logline("ERROR", "login 超时");
        else
            logline("ERROR", "login recv 失败: %s", strerror(errno));
        return LOGIN_ERR_TIMEOUT;
    }

    if (recv_packet[0] != 0x04) {
        print_packet("[Login recv]", recv_packet, 100);
        if (recv_packet[0] == 0x05) {
            int code = recv_packet[4];
            logline("ERROR", "登录失败，服务器返回错误码 0x%02x", code);
            return code;   /* 见 README 中的错误码对照表 */
        }
        logline("ERROR", "登录失败，未知响应");
        return LOGIN_ERR_UNKNOWN;
    }

    print_packet("[Login recv]", recv_packet, 100);
    logline("INFO", "登录成功");
    memcpy(auth_information, &recv_packet[23], 16);
    return LOGIN_OK;
}

static int keepalive_1(int sockfd, struct sockaddr_in *addr,
                       const unsigned char auth_information[16]) {
    unsigned char keepalive_1_packet1[8] = {0x07, 0x01, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00};
    unsigned char recv_packet1[1024], keepalive_1_packet2[42], recv_packet2[1024];
    memset(keepalive_1_packet2, 0, sizeof(keepalive_1_packet2));

    if (sendto(sockfd, (const char *)keepalive_1_packet1, 8, 0,
               (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        logline("ERROR", "keepalive_1 发送失败: %s", strerror(errno));
        return -1;
    }
    print_packet("[Keepalive1 sent]", keepalive_1_packet1, 8);

    socklen_t addrlen = sizeof(*addr);
    for (;;) {
        int n = recvfrom(sockfd, (char *)recv_packet1, sizeof(recv_packet1), 0,
                         (struct sockaddr *)addr, &addrlen);
        if (n < 0) {
            logline("WARN", "keepalive_1 挑战超时: %s", strerror(errno));
            return -1;
        }
        print_packet("[Keepalive1 challenge_recv]", recv_packet1, 100);
        if (recv_packet1[0] == 0x07) break;
        else if (recv_packet1[0] == 0x4d) continue;  /* 通知包，忽略 */
        else {
            logline("WARN", "keepalive_1 收到异常响应");
            return -1;
        }
    }

    unsigned char keepalive1_seed[4] = {0};
    int encrypt_type;
    unsigned char crc[8] = {0};
    memcpy(keepalive1_seed, &recv_packet1[8], 4);
    encrypt_type = keepalive1_seed[0] & 3;
    gen_crc(keepalive1_seed, encrypt_type, crc);

    keepalive_1_packet2[0] = 0xff;
    memcpy(keepalive_1_packet2 + 8, keepalive1_seed, 4);
    memcpy(keepalive_1_packet2 + 12, crc, 8);
    memcpy(keepalive_1_packet2 + 20, auth_information, 16);
    keepalive_1_packet2[36] = rand() & 0xff;
    keepalive_1_packet2[37] = rand() & 0xff;

    if (sendto(sockfd, (const char *)keepalive_1_packet2, 42, 0,
               (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        logline("ERROR", "keepalive_1 发送失败: %s", strerror(errno));
        return -1;
    }
    print_packet("[Keepalive1 pkt2 sent]", keepalive_1_packet2, 42);

    addrlen = sizeof(*addr);
    int n = recvfrom(sockfd, (char *)recv_packet2, sizeof(recv_packet2), 0,
                     (struct sockaddr *)addr, &addrlen);
    if (n < 0) {
        logline("WARN", "keepalive_1 响应超时: %s", strerror(errno));
        return -1;
    }
    print_packet("[Keepalive1 recv]", recv_packet2, 100);
    if (recv_packet2[0] != 0x07) {
        logline("WARN", "keepalive_1 响应异常");
        return -1;
    }
    return 0;
}

static void keepalive_2_packetbuilder(unsigned char *pkt, int counter,
                                      int filepacket, int type) {
    memset(pkt, 0, 40);
    pkt[0] = 0x07;
    pkt[1] = (unsigned char)counter;
    pkt[2] = 0x28;
    pkt[4] = 0x0b;
    pkt[5] = (unsigned char)type;
    if (filepacket) {
        pkt[6] = 0x0f;
        pkt[7] = 0x27;
    } else {
        pkt[6] = KEEP_ALIVE_VER_HI;
        pkt[7] = KEEP_ALIVE_VER_LO;
    }
    pkt[8] = 0x2f;
    pkt[9] = 0x12;
    /* type==3 时 [28..31] 填 host_ip = 0.0.0.0（保持与可用客户端一致） */
}

static int keepalive_2(int sockfd, struct sockaddr_in *addr,
                       int *keepalive_counter, int *first) {
    unsigned char keepalive_2_packet[40], recv_packet[1024], tail[4];
    socklen_t addrlen = sizeof(*addr);

    if (*first) {
        keepalive_2_packetbuilder(keepalive_2_packet, *keepalive_counter % 0xFF, *first, 1);
        (*keepalive_counter)++;
        if (sendto(sockfd, (const char *)keepalive_2_packet, 40, 0,
                   (struct sockaddr *)addr, sizeof(*addr)) < 0)
            return -1;
        print_packet("[Keepalive2_file sent]", keepalive_2_packet, 40);

        if (recvfrom(sockfd, (char *)recv_packet, sizeof(recv_packet), 0,
                     (struct sockaddr *)addr, &addrlen) < 0)
            return -1;
        print_packet("[Keepalive2_file recv]", recv_packet, 40);

        if (recv_packet[0] == 0x07) {
            if (recv_packet[2] == 0x10) {
                /* 文件包已收到 */
            } else if (recv_packet[2] != 0x28) {
                logline("WARN", "keepalive_2 文件包响应异常");
                return -1;
            }
        } else {
            logline("WARN", "keepalive_2 文件包响应异常");
            return -1;
        }
    }

    *first = 0;
    keepalive_2_packetbuilder(keepalive_2_packet, *keepalive_counter % 0xFF, *first, 1);
    (*keepalive_counter)++;
    if (sendto(sockfd, (const char *)keepalive_2_packet, 40, 0,
               (struct sockaddr *)addr, sizeof(*addr)) < 0)
        return -1;
    print_packet("[Keepalive2_A sent]", keepalive_2_packet, 40);

    if (recvfrom(sockfd, (char *)recv_packet, sizeof(recv_packet), 0,
                 (struct sockaddr *)addr, &addrlen) < 0)
        return -1;
    print_packet("[Keepalive2_B recv]", recv_packet, 40);

    if (recv_packet[0] == 0x07) {
        if (recv_packet[2] != 0x28) {
            logline("WARN", "keepalive_2 响应异常");
            return -1;
        }
    } else {
        logline("WARN", "keepalive_2 响应异常");
        return -1;
    }
    memcpy(tail, &recv_packet[16], 4);

    keepalive_2_packetbuilder(keepalive_2_packet, *keepalive_counter % 0xFF, *first, 3);
    memcpy(keepalive_2_packet + 16, tail, 4);
    (*keepalive_counter)++;
    if (sendto(sockfd, (const char *)keepalive_2_packet, 40, 0,
               (struct sockaddr *)addr, sizeof(*addr)) < 0)
        return -1;
    print_packet("[Keepalive2_C sent]", keepalive_2_packet, 40);

    if (recvfrom(sockfd, (char *)recv_packet, sizeof(recv_packet), 0,
                 (struct sockaddr *)addr, &addrlen) < 0)
        return -1;
    print_packet("[Keepalive2_D recv]", recv_packet, 40);

    if (recv_packet[0] == 0x07) {
        if (recv_packet[2] != 0x28) {
            logline("WARN", "keepalive_2 响应异常");
            return -1;
        }
    } else {
        logline("WARN", "keepalive_2 响应异常");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 主流程                                                            */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    const char *conf = argc > 1 ? argv[1] : "/etc/drcom.conf";

    logline("INFO", "JLU DrCOM 客户端启动");
    if (parse_config(conf) != 0)
        return 1;

    srand((unsigned int)time(NULL));

    logline("INFO", "认证服务器: %s", server);
    logline("INFO", "用户名: %s", username);
    logline("INFO", "MAC: %02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons((uint16_t)drcom_port);
    if (inet_pton(AF_INET, server, &dest_addr.sin_addr) != 1) {
        logline("ERROR", "无效的服务器地址: %s", server);
        return 1;
    }

    int reconnect_backoff = 5;   /* 重连间隔，单位秒，失败后翻倍 */

    for (;;) {
        /* 新建 socket */
        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            logline("ERROR", "创建 socket 失败: %s", strerror(errno));
            sleep(reconnect_backoff);
            continue;
        }

        int reuse = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        struct timeval tv = {3, 0};
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        if (local_ip[0]) {
            if (inet_pton(AF_INET, local_ip, &bind_addr.sin_addr) != 1) {
                logline("ERROR", "无效的 local_ip: %s", local_ip);
                close(sockfd);
                sleep(reconnect_backoff);
                continue;
            }
        } else {
            bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        }
        bind_addr.sin_port = htons((uint16_t)drcom_port);
        if (bind(sockfd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
            logline("ERROR", "绑定 61440 端口失败: %s", strerror(errno));
            close(sockfd);
            sleep(reconnect_backoff);
            continue;
        }

        unsigned char seed[4];
        unsigned char auth_information[16];
        int logged_in = 0;

        if (dhcp_challenge(sockfd, &dest_addr, seed) == 0) {
            usleep(200 * 1000);
            int rc = dhcp_login(sockfd, &dest_addr, seed, auth_information);
            if (rc == LOGIN_OK) {
                logged_in = 1;
                reconnect_backoff = 5;  /* 登录成功后重置退避 */
            } else {
                logline("INFO", "%d 秒后重试登录 ...", reconnect_backoff);
            }
        } else {
            logline("INFO", "%d 秒后重试 ...", reconnect_backoff);
        }

        if (logged_in) {
            int keepalive_counter = 0;
            int first = 1;
            for (;;) {
                if (keepalive_1(sockfd, &dest_addr, auth_information) != 0) {
                    logline("WARN", "keepalive 中断，准备重新登录");
                    break;
                }
                usleep(200 * 1000);
                if (keepalive_2(sockfd, &dest_addr, &keepalive_counter, &first) != 0)
                    continue;   /* keepalive_2 失败则立即重试 keepalive_1 */
                usleep(20 * 1000 * 1000);   /* 20 秒保活间隔 */
            }
        }

        close(sockfd);
        sleep(reconnect_backoff);
        if (reconnect_backoff < 300)
            reconnect_backoff *= 2;
    }

    return 0;
}

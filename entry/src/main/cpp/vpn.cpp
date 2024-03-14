//
// Created on 2024/3/14.
//
// Node APIs are not fully supported. To solve the compilation error of the interface cannot be found,
// please include "napi/native_api.h".

#include "napi/native_api.h"
#include "hilog/log.h"

#include <cstring>
#include <thread>
#include <js_native_api.h>
#include <js_native_api_types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <sys/time.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 2048

#define VPN_LOG_TAG "NetMgrVpn"
#define VPN_LOG_DOMAIN 0x15b0
#define MAKE_FILE_NAME (strrchr(__FILE__, '/') + 1)

#define NETMANAGER_VPN_LOGE(fmt, ...)                                                                                  \
    OH_LOG_Print(LOG_APP, LOG_ERROR, VPN_LOG_DOMAIN, VPN_LOG_TAG, "vpn [%{public}s %{public}d] " fmt, MAKE_FILE_NAME,  \
                 __LINE__, ##__VA_ARGS__)

#define NETMANAGER_VPN_LOGI(fmt, ...)                                                                                  \
    OH_LOG_Print(LOG_APP, LOG_INFO, VPN_LOG_DOMAIN, VPN_LOG_TAG, "vpn [%{public}s %{public}d] " fmt, MAKE_FILE_NAME,   \
                 __LINE__, ##__VA_ARGS__)

#define NETMANAGER_VPN_LOGD(fmt, ...)                                                                                  \
    OH_LOG_Print(LOG_APP, LOG_DEBUG, VPN_LOG_DOMAIN, VPN_LOG_TAG, "vpn [%{public}s %{public}d] " fmt, MAKE_FILE_NAME,  \
                 __LINE__, ##__VA_ARGS__)

struct FdInfo {
    int32_t tunFd = 0;
    int32_t tunnelFd = 0;
    struct sockaddr_in serverAddr;
};

static FdInfo fdInfo;
static bool threadRunF = false;
static std::thread threadt1;
static std::thread threadt2;

// 获取对应字符串数据, 用于获取udp server 的IP地址
static constexpr const int MAX_STRING_LENGTH = 1024;
std::string GetStringFromValueUtf8(napi_env env, napi_value value) {
    std::string result;
    char str[MAX_STRING_LENGTH] = {0};
    size_t length = 0;
    napi_get_value_string_utf8(env, value, str, MAX_STRING_LENGTH, &length);
    if (length > 0) {
        return result.append(str, length);
    }
    return result;
}

void HandleReadTunfd(FdInfo fdInfo) {
    uint8_t buffer[BUFFER_SIZE] = {0};
    while (threadRunF) {
        int ret = read(fdInfo.tunFd, buffer, sizeof(buffer));
        if (ret <= 0) {
            if (errno != 11) {
                NETMANAGER_VPN_LOGE("read tun device error: %{public}d, tunfd: %{public}d", errno, fdInfo.tunFd);
            }
            continue;
        }

        // 读取到虚拟网卡的数据，通过udp隧道，发送给服务器
        NETMANAGER_VPN_LOGD("buffer: %{public}s, len: %{public}d", buffer, ret);
        ret = sendto(fdInfo.tunnelFd, buffer, ret, 0, (struct sockaddr *)&fdInfo.serverAddr, sizeof(fdInfo.serverAddr));
        if (ret <= 0) {
            NETMANAGER_VPN_LOGE("send to server[%{public}s:%{public}d] failed, ret: %{public}d, error: %{public}s",
                                inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port), ret,
                                strerror(errno));
            continue;
        }
    }
}

void HandleTcpReceived(FdInfo fdInfo) {
    int addrlen = sizeof(struct sockaddr_in);
    uint8_t buffer[BUFFER_SIZE] = {0};
    while (threadRunF) {
        int length = recvfrom(fdInfo.tunnelFd, buffer, sizeof(buffer), 0, (struct sockaddr *)&fdInfo.serverAddr,
                              (socklen_t *)&addrlen);
        if (length < 0) {
            if (errno != 11) {
                NETMANAGER_VPN_LOGE("read tun device error: %{public}d，tunnelfd: %{public}d", errno, fdInfo.tunnelFd);
            }
            continue;
        }

        // 接收到udp server的数据，写入到虚拟网卡中
        NETMANAGER_VPN_LOGD("from [%{public}s:%{public}d] data: %{public}s, len: %{public}d",
                            inet_ntoa(fdInfo.serverAddr.sin_addr), ntohs(fdInfo.serverAddr.sin_port), buffer, length);
        int ret = write(fdInfo.tunFd, buffer, length);
        if (ret <= 0) {
            NETMANAGER_VPN_LOGE("error Write To Tunfd, errno: %{public}d", errno);
        }
    }
}

static napi_value UdpConnect(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int32_t port = 0;
    napi_get_value_int32(env, args[1], &port);
    std::string ipAddr = GetStringFromValueUtf8(env, args[0]);

    NETMANAGER_VPN_LOGI("ip: %{public}s port: %{public}d", ipAddr.c_str(), port);

    // 建立udp隧道
    int32_t sockFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockFd == -1) {
        NETMANAGER_VPN_LOGE("socket() error");
        return 0;
    }

    struct timeval timeout = {1, 0};
    setsockopt(sockFd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(struct timeval));

    memset(&fdInfo.serverAddr, 0, sizeof(fdInfo.serverAddr));
    fdInfo.serverAddr.sin_family = AF_INET;
    fdInfo.serverAddr.sin_addr.s_addr = inet_addr(ipAddr.c_str()); // server's IP addr
    fdInfo.serverAddr.sin_port = htons(port);                      // port

    NETMANAGER_VPN_LOGI("Connection successful");

    napi_value tunnelFd;
    napi_create_int32(env, sockFd, &tunnelFd);
    return tunnelFd;
}

static napi_value StartVpn(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    napi_get_value_int32(env, args[0], &fdInfo.tunFd);
    napi_get_value_int32(env, args[1], &fdInfo.tunnelFd);

    if (threadRunF) {
        threadRunF = false;
        threadt1.join();
        threadt2.join();
    }

    // 启动两个线程, 一个处理读取虚拟网卡的数据，另一个接收服务端的数据
    threadRunF = true;
    std::thread tt1(HandleReadTunfd, fdInfo);
    std::thread tt2(HandleTcpReceived, fdInfo);

    threadt1 = std::move(tt1);
    threadt2 = std::move(tt2);

    NETMANAGER_VPN_LOGI("StartVpn successful");

    napi_value retValue;
    napi_create_int32(env, 0, &retValue);
    return retValue;
}

static napi_value StopVpn(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int32_t tunnelFd;
    napi_get_value_int32(env, args[0], &tunnelFd);
    if (tunnelFd) {
        close(tunnelFd);
        tunnelFd = 0;
    }

    // 停止两个线程
    if (threadRunF) {
        threadRunF = false;
        threadt1.join();
        threadt2.join();
    }

    NETMANAGER_VPN_LOGI("StopVpn successful");

    napi_value retValue;
    napi_create_int32(env, 0, &retValue);
    return retValue;
}

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"udpConnect", nullptr, UdpConnect, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"startVpn", nullptr, StartVpn, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stopVpn", nullptr, StopVpn, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module demoModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = ((void *)0),
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void) { napi_module_register(&demoModule); }
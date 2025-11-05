#include "udp_mgr.h"
#define TAG "UDP_MGR"

static udp_mgr_t udp_manager = {
    .sock = -1,
    .ready = false,
};

udp_mgr_t udp_mgr_get()
{
    return udp_manager;
}

bool udp_mgr_init(const char *ip, uint16_t port)
{
    if (udp_manager.ready)
    {
        ESP_LOGW(TAG, "UDP 已初始化");
        return true;
    }
    udp_manager.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp_manager.sock < 0)
    {
        ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
        udp_manager.ready = false;
        return false;
    }

    int yes = 1;
    setsockopt(udp_manager.sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&udp_manager.dest, 0, sizeof(udp_manager.dest));
    udp_manager.dest.sin_family = AF_INET;
    udp_manager.dest.sin_port = htons(port);
    udp_manager.dest.sin_addr.s_addr = inet_addr(ip);

    udp_manager.ready = true;
    ESP_LOGI(TAG, "UDP init ok: %s:%d", ip, port);
    return true;
}

bool udp_mgr_send(const void *data, size_t len)
{
    if (!udp_manager.ready)
    {
        ESP_LOGW(TAG, "UDP 未初始化，无法发送数据");
        return false;
    }

    int err = sendto(udp_manager.sock, data, len, 0,
                     (struct sockaddr *)&udp_manager.dest, sizeof(udp_manager.dest));
    if (err < 0)
    {
        ESP_LOGE(TAG, "sendto failed: errno=%d", errno);
        udp_manager.ready = false;
        close(udp_manager.sock);
        udp_manager.sock = -1;
        return false;
    }
    return true;
}

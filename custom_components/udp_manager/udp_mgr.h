#ifndef UPD_MGR_H
#define UPD_MGR_H

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"

typedef struct
{
    int sock;
    struct sockaddr_in dest;
    bool ready;
} udp_mgr_t;

bool udp_mgr_init(const char *ip, uint16_t port);

bool udp_mgr_send(const void *data, size_t len);

// bool udp_reset(const char *ip, uint16_t port);

udp_mgr_t udp_mgr_get();
#endif
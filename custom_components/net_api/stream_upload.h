#ifndef HTTP_B_H
#define HTTP_B_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_event.h"
#include <esp_log.h>
#include "mqtt_ssl.h"
#include "udp_mgr.h"

typedef struct
{
    uint8_t data[1024];
    size_t len;
} mqtt_upload_chunk_t;

void stream_upload_start(esp_http_client_handle_t *client);
void stream_upload_stop(esp_http_client_handle_t *client);
void stream_upload_write(esp_http_client_handle_t *client, char *data, size_t len);

void mqtt_upload_write(mqtt_upload_chunk_t data);

void udp_upload_write(const void *data, size_t len);
#endif
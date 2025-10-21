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

void stream_upload_start(esp_http_client_handle_t *client);
void stream_upload_stop(esp_http_client_handle_t *client);
void stream_upload_write(esp_http_client_handle_t *client, char *data, size_t len);

#endif
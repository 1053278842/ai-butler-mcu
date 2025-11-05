#ifndef MQTT_SSL_H
#define MQTT_SSL_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_partition.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_partition.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_ota_ops.h"
#include <time.h>
#include <sys/time.h>
// #include "ca_cert.h"
#include <sys/param.h>
#include "cJSON.h"
#include "sdkconfig.h"
#include "board_light.h"
#include "mqtt_ssl_config.h"
#include "light_manager.h"
#include "i2s_voice.h"

void start_mqtt_ssl(void);
void stop_mqtt_ssl(void);

esp_mqtt_client_handle_t esp_mqtt_client_get_global_handle(void);
#endif
#pragma once

#define EASY_WIFI_AP_SSID "ESP32_Device"
#define EASY_WIFI_AP_PASS "12345678"
#define EASY_WIFI_AP_CHANNEL 1

#define EASY_WIFI_MAX_AP_RECORDS 16
#define EASY_WIFI_AP_JSON_BUF_SIZE 1024
#define EASY_WIFI_CONN_JSON_BUF_SIZE 128

typedef enum
{
    EASY_WIFI_STATE_INIT,
    EASY_WIFI_STATE_CONNECTING,
    EASY_WIFI_STATE_CONNECTED,
    EASY_WIFI_STATE_AP

}easy_wifi_state_t;


void easy_wifi_init(void);
char *easy_wifi_get_ip(void);
void easy_wifi_set_disconnect_cb(void (*cb)(void));
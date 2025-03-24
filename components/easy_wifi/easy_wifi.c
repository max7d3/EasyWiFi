#include "easy_wifi.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

extern const uint8_t portal_html_start[] asm("_binary_portal_html_start");
extern const uint8_t portal_html_start_end[]   asm("_binary_portal_html_start_end");

static const char *TAG = "easy_wifi";

static easy_wifi_state_t easy_wifi_state = EASY_WIFI_STATE_INIT;

static EventGroupHandle_t wifi_event_group;

static esp_netif_t *netif_sta;
static esp_netif_t *netif_ap;

static wifi_ap_record_t wifi_ap_info[EASY_WIFI_MAX_AP_RECORDS];
static uint16_t wifi_ap_count;

static char ip_addr_str[16];

httpd_handle_t config_server;

void (*disconnect_cb)(void);

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        if(easy_wifi_state == EASY_WIFI_STATE_CONNECTING)
        {
            ESP_LOGI(TAG, "Connecting to last known AP...");
            esp_wifi_connect();
        }
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if(easy_wifi_state == EASY_WIFI_STATE_CONNECTING)
        {
            ESP_LOGE(TAG,"Connecting to last known AP failed!");
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        else if(easy_wifi_state == EASY_WIFI_STATE_CONNECTED)
        {
            ESP_LOGW(TAG, "Disconnected from AP!");
            if(disconnect_cb != NULL)
            {
                disconnect_cb();
            }
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        sprintf(ip_addr_str, IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}


static esp_err_t check_for_stored_config(void)
{
    wifi_config_t stored_config;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &stored_config);

    if (err == ESP_OK) 
    {
        if (strlen((char*)stored_config.sta.ssid) > 0) 
        {
            ESP_LOGI(TAG, "Found automatically stored WiFi credentials for SSID: %s", stored_config.sta.ssid);
            return ESP_OK;
        }
    }
    
    return ESP_FAIL;
}

static void scan_wifi(void)
{
    ESP_ERROR_CHECK(esp_wifi_scan_start(NULL, true));

    uint16_t number = EASY_WIFI_MAX_AP_RECORDS;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, wifi_ap_info));
    wifi_ap_count = number;

    ESP_ERROR_CHECK(esp_wifi_scan_stop());
}

static void connect(char *ssid, char *pass)
{
    wifi_config_t wifi_config = {
        .sta = 
        {
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = 
            {
                .required = false,
            },
        },
    };

    strcpy((char*)wifi_config.sta.ssid, ssid);
    strcpy((char*)wifi_config.sta.password, pass);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "Connecting to %s...", ssid);
    ESP_ERROR_CHECK(esp_wifi_connect());
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    const char* resp_str = (const char*) req->user_ctx;
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t wifi_get_handler(httpd_req_t *req)
{
    char buf[EASY_WIFI_AP_JSON_BUF_SIZE];

    scan_wifi();

    sprintf(buf, "{\"nets_cnt\":%d,\"nets\":[", wifi_ap_count);

    uint8_t first = 1;

    for (uint16_t i = 0; i < wifi_ap_count; i++)
    {
        if (first)
        {
            first = 0;
            sprintf(buf + strlen(buf), "{\"SSID\":\"%s\",\"RSSI\":%d}", (char*)wifi_ap_info[i].ssid, wifi_ap_info[i].rssi); 
        }
        else
        {
            sprintf(buf + strlen(buf), ",{\"SSID\":\"%s\",\"RSSI\":%d}", (char*)wifi_ap_info[i].ssid, wifi_ap_info[i].rssi); 
        }
    }

    sprintf(buf + strlen(buf), "]}");
    ESP_LOGI(TAG, "Sending WiFi scan results: \n\n %s", buf);
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    char buf[EASY_WIFI_CONN_JSON_BUF_SIZE] = {0};
    char respone_buf[128] = {0};
    int ret, remaining = req->content_len;

    while (remaining > 0) 
    {
        if ((ret = httpd_req_recv(req, buf, EASY_WIFI_CONN_JSON_BUF_SIZE)) <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }

    char *ssid, *pass;
    ssid = strstr(buf, "ssid") + 7;
    pass = strstr(buf, "pass") + 7;

    char* end = strchr(ssid, '"');
    *end = '\0';
    end = strchr(pass, '"');
    *end = '\0';

    easy_wifi_state = EASY_WIFI_STATE_CONNECTING;
    connect(ssid, pass);

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if(bits & WIFI_CONNECTED_BIT) 
    {
        easy_wifi_state = EASY_WIFI_STATE_AP; // Just any state that won't trigger the disconnect callback, we are rebooting immediately after this.
        ESP_LOGI(TAG, "Connected to %s successfully!", ssid);
        sprintf(respone_buf, "{\"text\":\"Connected to %s successfully!\\nGot IP: %s\\nDevice will reboot now, you can close this page.\"}", ssid, ip_addr_str);
        httpd_resp_send(req, respone_buf, HTTPD_RESP_USE_STRLEN);

        esp_wifi_disconnect();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    } 
    else if (bits & WIFI_FAIL_BIT) 
    {
        xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);
        ESP_LOGE(TAG, "Connection error!");
        sprintf(respone_buf, "{\"text\":\"Connection error!\\nPlease check the credentials.\"}");
    } 
    else 
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        sprintf(respone_buf, "{\"text\":\"Unexpected error.\"}");
    }
    
    httpd_resp_send(req, respone_buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t start_config_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&config_server, &config) == ESP_OK) 
    {
        httpd_uri_t index_uri = 
        {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = index_get_handler,
            .user_ctx = (char *)portal_html_start
        };

        httpd_uri_t wifi_get_uri = {
            .uri      = "/wifi",
            .method   = HTTP_GET,
            .handler  = wifi_get_handler,
            .user_ctx = NULL
        };

        httpd_uri_t wifi_post_uri = {
            .uri      = "/wifi",
            .method   = HTTP_POST,
            .handler  = wifi_post_handler,
            .user_ctx = NULL
        };

        httpd_register_uri_handler(config_server, &index_uri);
        httpd_register_uri_handler(config_server, &wifi_get_uri);
        httpd_register_uri_handler(config_server, &wifi_post_uri);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    return ESP_OK;
}


static void start_ap_and_config_portal(void)
{
    easy_wifi_state = EASY_WIFI_STATE_AP;

    ESP_ERROR_CHECK(esp_wifi_stop());

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = EASY_WIFI_AP_SSID,
            .ssid_len = strlen(EASY_WIFI_AP_SSID),
            .channel = EASY_WIFI_AP_CHANNEL,
            .password = EASY_WIFI_AP_PASS,
            .max_connection = 3,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    netif_ap = esp_netif_create_default_wifi_ap();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(start_config_server());
    
}

void easy_wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi...");

    wifi_event_group = xEventGroupCreate();

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    netif_sta = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_LOGI(TAG, "Checking for stored WiFi credentials...");
    if(check_for_stored_config() == ESP_OK)
    {
        easy_wifi_state = EASY_WIFI_STATE_CONNECTING;

        ESP_ERROR_CHECK(esp_wifi_start());
    
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    
        if(bits & WIFI_CONNECTED_BIT) 
        {
            easy_wifi_state = EASY_WIFI_STATE_CONNECTED;
            ESP_LOGI(TAG, "Wifi connection successful!");
        } 
        else if (bits & WIFI_FAIL_BIT) 
        {
            // Start AP mode
            ESP_LOGW(TAG, "Connection failed, starting AP mode and configuration server...");
            xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);
            start_ap_and_config_portal();
            while(1) { vTaskDelay(1000 / portTICK_PERIOD_MS); } // Halt rest of the code. In case of successful connection, program will restart anyway.
        } 
        else 
        {
            ESP_LOGE(TAG, "UNEXPECTED EVENT! HALTING...");
            while(1) { vTaskDelay(1000 / portTICK_PERIOD_MS); }
        }
    }
    else
    {
        ESP_LOGW(TAG, "No stored WiFi credentials found, starting AP mode and configuration server...");
        start_ap_and_config_portal();
    }
}

void easy_wifi_set_disconnect_cb(void (*cb)(void))
{
    disconnect_cb = cb;
}

char *easy_wifi_get_ip(void)
{
    return ip_addr_str;
}

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "easy_wifi.h"


void callback(void)
{
    printf("This should be printed after WiFi disconnection\n");
}

void app_main(void)
{
    printf("Hello world!\n");

    easy_wifi_set_disconnect_cb(callback);
    easy_wifi_init();

    printf("This should be printed after WiFi initialization\n");
    printf("IP: %s\n", easy_wifi_get_ip());

    while(1) 
    {
        printf("Hello world from loop!\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
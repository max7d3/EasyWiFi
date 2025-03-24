# EasyWiFi

EasyWiFi is a simple component for ESP-IDF that allows to skip the boilerplate Wi-Fi code and provides the minimum functionality needed to get ESP32 connected to a Wi-Fi network. This component is designed to be simple, easy to use and can be expanded in the future if needed.

## Features

- Simplified Wi-Fi initialization and connection
- Automatic connection to the last known Wi-Fi network (using data that ESP-IDF stores by default)
- Access Point (AP) mode for configuration
- Web-based configuration portal
- Callback for Wi-Fi disconnection events

### Usage

1. Initialize EasyWiFi in `app_main` function:

    ```c
    void app_main(void)
    {
        easy_wifi_init();
    }
    ```
Code after init function won't be executed if WiFi is not connected.

2. Optionally, set a callback function for Wi-Fi disconnection events:

    ```c
    void wifi_disconnect_callback(void)
    {
        printf("Wi-Fi disconnected!\n");
    }

    void app_main(void)
    {
        easy_wifi_set_disconnect_cb(wifi_disconnect_callback);
        easy_wifi_init();
    }
    ```

3. You can also get current IP in string format by calling `easy_wifi_get_ip()`. It ruturns pointer to that string.
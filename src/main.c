#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "wifi.h"



static const char* TAG = "main";
 

void app_main(){

    esp_err_t ret;

    static wifi_conf_t wifi_conf = {
        .hostname = "ESP32",
        .ntp_server = "pool.ntp.org",
    };

    wifi_t *smartconfig = wifi_new_smartconfig(&wifi_conf);
  
    if (smartconfig->init(smartconfig) == ESP_OK) {

        do {
            ret = smartconfig->connect(smartconfig);
        }
        while (ret != ESP_OK);
    }

    smartconfig->init_sntp(smartconfig);

    smartconfig->init_timezone(smartconfig);

}

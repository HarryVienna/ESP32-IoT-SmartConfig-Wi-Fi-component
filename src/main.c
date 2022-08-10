#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_sleep.h"
#include "esp_log.h"

#include "wifi.h"


RTC_DATA_ATTR static int boot_count = 0;

static const char* TAG = "main";
 

void app_main(){

    ++boot_count;
    ESP_LOGI(TAG, "Boot count: %d", boot_count);


    esp_err_t ret;

    static wifi_conf_t wifi_conf = {
        .aes_key = "ESP32EXAMPLECODE",
        .hostname = "ESP32",
        .ntp_server = "pool.ntp.org",
    };
    wifi_t *smartconfig = wifi_new_smartconfig(&wifi_conf);

    switch (esp_sleep_get_wakeup_cause()) {

        case ESP_SLEEP_WAKEUP_TIMER: {
            printf("ESP_SLEEP_WAKEUP_TIMER\n");
            break;
        }
        default: {
            printf("Not a deep sleep reset\n");
        }
    }

    if (smartconfig->init(smartconfig) == ESP_OK) {

        do {
            ret = smartconfig->connect(smartconfig);
        }
        while (ret != ESP_OK);

        smartconfig->init_sntp(smartconfig);

        smartconfig->init_timezone(smartconfig);
    }

    vTaskDelay((1000 * 60) / portTICK_PERIOD_MS); // Wait 1 minute



    const int deep_sleep_sec = 10;
    ESP_LOGI(TAG, "Entering deep sleep for %d seconds", deep_sleep_sec);
    smartconfig->stop(smartconfig);
    esp_deep_sleep(1000000LL * deep_sleep_sec);
}

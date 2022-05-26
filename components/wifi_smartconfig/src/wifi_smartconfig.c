#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_smartconfig.h"

#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi.h"

#define MAXIMUM_RETRY 5
#define NVS_NAMESPACE "WIFI"
#define TIMEZONE_VALUE "TZ"

static const char *TAG = "wifi_smartconfig";


/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define ESPTOUCH_DONE_BIT  BIT2

/* forward declaration */
static void connect_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

static int s_retry_num;

typedef struct {
    wifi_t parent;
    wifi_conf_t config;
} smartconfig_t;

/**
 * @brief Init wifi
 *
 * @param wifi_t type object
 */
static esp_err_t smartconfig_init(wifi_t *wifi)
{
    esp_err_t ret;

    smartconfig_t *smartconfig = __containerof(wifi, smartconfig_t, parent);

    //Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      if (nvs_flash_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init nvs flash");
        return ESP_FAIL;   
      }
    }

    // Create a new event group.
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    // Create default event loop
    if (esp_event_loop_create_default() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set create event loop");
        return ESP_FAIL;        
    }

    // Initialize the underlying TCP/IP stack
    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init netif");
        return ESP_FAIL;       
    }

    // Creates default WIFI ST
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    // Set hostname
    if (esp_netif_set_hostname(sta_netif, smartconfig->config.hostname)) {
        ESP_LOGE(TAG, "Failed to set hostname");
        return ESP_FAIL;
    }

    //  Init WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init wifi");
        return ESP_FAIL;       
    }
    
    // Set the WiFi operating mode
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mode");
        return ESP_FAIL;      
    }

    // Set storage to flash
    if (esp_wifi_set_storage(WIFI_STORAGE_FLASH) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set storage to flash");
        return ESP_FAIL;
    }

    // Register event handler
    if( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &connect_event_handler, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register handler");
        return ESP_FAIL;
    }
    if( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_event_handler, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register handler");
        return ESP_FAIL;
    }
    if( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &connect_event_handler, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register handler");
        return ESP_FAIL;
    }

    return ESP_OK;
}


static esp_err_t smartconfig_connect(wifi_t *wifi)
{
    EventBits_t bits;

    smartconfig_t *smartconfig = __containerof(wifi, smartconfig_t, parent);
    
    wifi_config_t wifi_config;

    if (esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get config");
        return ESP_FAIL;
    }
    if(strlen((const char*) wifi_config.sta.ssid)){
        ESP_LOGI(TAG, "Flash SSID:%s", wifi_config.sta.ssid);
        ESP_LOGI(TAG, "Flash Password:%s", wifi_config.sta.password);
    }
    else {
        ESP_LOGE(TAG, "Nothing in flash");
    }

    
    /* -------------- Try to connect with stored settings ------------- */
    s_retry_num = 0;

    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start wifi");
        return ESP_FAIL;       
    }

    ESP_LOGI(TAG, "WIFI startet. Waiting for events.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to ap SSID: %s password: %s", wifi_config.sta.ssid, wifi_config.sta.password);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s, password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
    } else {
        ESP_LOGE(TAG, "Unexpected event");
    }

    /* -------------- Try to connect with smartconfig ------------- */
    s_retry_num = 0;

    if (esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set smartconfig type");
        return ESP_FAIL;      
    }
    smartconfig_start_config_t smart_cfg;
    smart_cfg.enable_log = false;
    smart_cfg.esp_touch_v2_enable_crypt = true;
    smart_cfg.esp_touch_v2_key = smartconfig->config.aes_key;
    
    if (esp_smartconfig_start(&smart_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start smartconfig");
        return ESP_FAIL;   
    }

    do {
        bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | ESPTOUCH_DONE_BIT,
                pdTRUE,
                pdFALSE,
                portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "connected via smart config");
        } else if (bits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "Touch done");
            esp_smartconfig_stop();
            return ESP_OK;
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(TAG, "Failed to connect via smart config");
            esp_smartconfig_stop();
            esp_wifi_stop();
            return ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "Unexpected event");
            esp_smartconfig_stop();
            esp_wifi_stop();
            return ESP_FAIL;
        }
    } while (true);

}

static void connect_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        if (esp_wifi_connect() != ESP_OK) {
            ESP_LOGE(TAG, "Could not connect");
            esp_wifi_disconnect();
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP) {        
        ESP_LOGI(TAG,"WIFI_EVENT_STA_STOP");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {        
        ESP_LOGI(TAG,"WIFI_EVENT_STA_CONNECTED");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG,"WIFI_EVENT_STA_DISCONNECTED");
        if (s_retry_num < MAXIMUM_RETRY) {
            ESP_LOGI(TAG, "retry to connect to the AP");
            esp_wifi_connect();
            s_retry_num++;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG,"IP_EVENT_STA_GOT_IP");
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "SC_EVENT_SCAN_DONE");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "SC_EVENT_FOUND_CHANNEL");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "SC_EVENT_GOT_SSID_PSWD");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };
        uint8_t rvd_data[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        ESP_ERROR_CHECK( esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)) );
        ESP_LOGI(TAG, "RVD_DATA:%s", rvd_data);

        nvs_handle_t my_handle;
        ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle));
        ESP_ERROR_CHECK(nvs_set_str(my_handle, TIMEZONE_VALUE, (char*)rvd_data));
        ESP_ERROR_CHECK(nvs_commit(my_handle));
        nvs_close(my_handle);

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        
        if (esp_wifi_connect() != ESP_OK) {
            ESP_LOGE(TAG, "Could not connect");
            esp_wifi_disconnect();
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        ESP_LOGI(TAG, "SC_EVENT_SEND_ACK_DONE");
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

/**
 * @brief Callback function from time sync
 *
 */
void sync_callback(struct timeval *tv) {
  ESP_LOGI(TAG, "Syncing date/time: %s", ctime(&tv->tv_sec));
}

static esp_err_t smartconfig_init_sntp(wifi_t *wifi)
{
    smartconfig_t *smartconfig = __containerof(wifi, smartconfig_t, parent);
    
    ESP_LOGI(TAG, "Init SNTP");

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, smartconfig->config.ntp_server);
    sntp_set_time_sync_notification_cb(&sync_callback);
    sntp_init();
    
    return ESP_OK;
}

static esp_err_t smartconfig_init_timezone(wifi_t *wifi)
{
    esp_err_t err;

    smartconfig_t *smartconfig = __containerof(wifi, smartconfig_t, parent);
    
    ESP_LOGI(TAG, "Init timezone");

    char *timezone_value;
    size_t timezone_size = 0;

    nvs_handle_t my_handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if  (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS %d", err);
        return ESP_FAIL;
    }
    err = nvs_get_str(my_handle, TIMEZONE_VALUE, NULL, &timezone_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read size from NVS %d", err);
        return ESP_FAIL;
    }
    timezone_value = (char*)malloc(timezone_size);
    err = nvs_get_str(my_handle, TIMEZONE_VALUE, timezone_value, &timezone_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read value from NVS %d", err);
        return ESP_FAIL;
    }
    nvs_close(my_handle);

    ESP_LOGI(TAG, "NVS value %s", timezone_value);
    ESP_LOGI(TAG, "NVS size %d", timezone_size);

    // Set timezone
    setenv(TIMEZONE_VALUE, timezone_value, 1);
    tzset();
    
    return ESP_OK;
}



wifi_t *wifi_new_smartconfig(const wifi_conf_t *config)
{
    smartconfig_t *smartconfig = calloc(1, sizeof(smartconfig_t));
    
    smartconfig->config.aes_key = config->aes_key;
    smartconfig->config.hostname = config->hostname;
    smartconfig->config.ntp_server = config->ntp_server;

    smartconfig->parent.init = smartconfig_init;
    smartconfig->parent.connect = smartconfig_connect;
    smartconfig->parent.init_sntp = smartconfig_init_sntp;
    smartconfig->parent.init_timezone = smartconfig_init_timezone;


    return &smartconfig->parent;
}

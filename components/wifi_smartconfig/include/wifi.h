#pragma once

#include "esp_err.h"

/**
* @brief Wifi Type
*
*/
typedef struct wifi_s wifi_t;



/**
* @brief Declare of Step motor Type
*
*/
struct wifi_s {


    esp_err_t (*init)(wifi_t *wifi);

    esp_err_t (*connect)(wifi_t *wifi);

    esp_err_t (*smart_connect)(wifi_t *wifi);

    esp_err_t (*init_sntp)(wifi_t *wifi);

    esp_err_t (*init_timezone)(wifi_t *wifi);
    
};

/**
* @brief Wifi Configuration Type
*
*/
typedef struct wifi_conf_s {
    char *hostname;
    char *ntp_server;
} wifi_conf_t;


/**
* @brief Install a new Wifi driver 
*
* @param config: wifi configuration
* @return
*      wifi instance or NULL
*/
wifi_t *wifi_new_smartconfig(const wifi_conf_t *config);



set(component_srcs "src/wifi_wlan.c")

idf_component_register(SRCS "${component_srcs}"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS ""
                       PRIV_REQUIRES "nvs_flash"
                       REQUIRES "")

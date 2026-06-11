#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { char ssid[33]; char pass[33]; char url[32]; bool ok; } wifi_drop_info_t;
void wifi_drop_start(wifi_drop_info_t* out);   // bring up AP + HTTP server
void wifi_drop_stop(void);                     // stop server + WiFi
#ifdef __cplusplus
}
#endif

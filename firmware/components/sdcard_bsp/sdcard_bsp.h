#ifndef SDCARD_BSP_H
#define SDCARD_BSP_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// Mount the SDMMC TF card (1-line) at "/sdcard". Safe to call once at boot;
// logs and leaves the card unmounted on failure (no abort).
void sdcard_init(void);
bool sdcard_mounted(void);

#ifdef __cplusplus
}
#endif
#endif

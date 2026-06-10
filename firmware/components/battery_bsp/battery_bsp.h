#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void batt_init(void);     // ADC1 oneshot ch3 + 12dB + curve-fitting cali
int  batt_read_mv(void);  // average ~16 reads -> calibrated mV x3 (battery mV); 0 on failure
#ifdef __cplusplus
}
#endif

#pragma once
#include "rsvp/rtctime.hpp"

void rtc_init();                          // add the 0x51 device + set 24h mode
bool rtc_valid();                         // false if the oscillator-stop (OS) flag is set
bool rtc_get(rsvp::RtcTime& out);         // read sec/min/hour; false on I2C error
bool rtc_set(const rsvp::RtcTime& t);     // write sec(OS cleared)/min/hour; false on error

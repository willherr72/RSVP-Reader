#pragma once
#include <cstdint>

void imu_init();                                            // add 0x6b + enable accelerometer
bool imu_read_accel(int16_t& ax, int16_t& ay, int16_t& az); // raw counts; false on I2C error

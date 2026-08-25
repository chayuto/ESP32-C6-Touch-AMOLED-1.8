#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct { float temp_c; float humid_pct; uint8_t battery_pct; } govee_reading_t;

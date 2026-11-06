#pragma once
#define __COMMON_H__
#include <stdint.h>
#define LEDS_HAND_LEFT 33
#define LEDS_HAND_RIGHT 36
#define UNDERGLOW_LEDS_LEFT_SIDE 53
#define UNDERGLOW_LEDS_RIGHT_SIDE 54
extern uint32_t test_time;
inline uint32_t millis() { return test_time; }

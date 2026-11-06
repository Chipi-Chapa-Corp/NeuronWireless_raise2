#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct { uint8_t h,s,v; } hsv_t;
typedef struct { uint8_t r,g,b; } rgb_t;
#ifdef __cplusplus
extern "C" {
#endif
rgb_t hsv_to_rgb_nocie(hsv_t hsv);
#ifdef __cplusplus
}
#endif

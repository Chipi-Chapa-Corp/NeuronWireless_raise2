#pragma once
#include <stdint.h>
struct dl_timer_t { uint32_t deadline; };
inline void timer_set_ms(dl_timer_t *timer, uint32_t delay) { timer->deadline=delay; }
inline bool timer_check(dl_timer_t *) { return false; }

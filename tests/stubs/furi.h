#pragma once
#include <stdint.h>
uint32_t furi_get_tick(void);
uint32_t furi_ms_to_ticks(uint32_t ms);
uint32_t furi_kernel_get_tick_frequency(void);

#pragma once
#include <stdint.h>
bool tud_cdc_n_ready(uint8_t);
uint32_t tud_cdc_n_write_available(uint8_t);
uint32_t tud_cdc_n_write(uint8_t, const void *, uint32_t);
uint32_t tud_cdc_n_write_flush(uint8_t);

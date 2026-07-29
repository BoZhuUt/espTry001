#pragma once

#include <Arduino.h>

#if defined(PYXIS_BOARD_WAVESHARE_147) && \
    defined(PYXIS_BOARD_WAVESHARE_28)
#error "Select exactly one Pyxis board target"
#elif defined(PYXIS_BOARD_WAVESHARE_147)
#include "boards/waveshare_147_config.h"
#elif defined(PYXIS_BOARD_WAVESHARE_28)
#include "boards/waveshare_28_config.h"
#else
#error "No Pyxis board target selected by PlatformIO"
#endif

// Compile-time oscilloscope test. Kept as a macro because main.cpp uses #if.
#ifndef RS485_TX_TEST_MODE
#define RS485_TX_TEST_MODE 0
#endif

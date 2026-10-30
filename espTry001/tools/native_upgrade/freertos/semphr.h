#pragma once
using SemaphoreHandle_t = void *;
constexpr int portMAX_DELAY = -1;
inline void *xSemaphoreCreateMutex() { static int value; return &value; }
inline void xSemaphoreTake(void *, int) {} inline void xSemaphoreGive(void *) {}

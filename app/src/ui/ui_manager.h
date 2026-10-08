#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "device_status.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_manager_init(void);

/**
 * @brief Wakes the display if it is sleeping. Safe to call from any thread
 * (not from ISR context); the LVGL thread picks it up on its next loop.
 */
void ui_manager_wake(void);

#ifdef __cplusplus
}
#endif

#endif
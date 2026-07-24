#ifndef UI_MANAGER_H
#define UI_MANAGER_H

typedef enum {
  SCREEN_SPLASH = 0,
  SCREEN_OVERVIEW,
  SCREEN_TILT,
  SCREEN_ENVIRONMENT,
  SCREEN_CAN,
  SCREEN_POWER,
  SCREEN_COUNT
} screen_id_t;

#ifdef __cplusplus
extern "C" {
#endif

void ui_manager_init(void);

#ifdef __cplusplus
}
#endif

#endif
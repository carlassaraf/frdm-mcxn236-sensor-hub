#ifndef TILT_SIM_H
#define TILT_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

// Starts a self-rescheduling work item that feeds synthetic X/Y/Z readings
// into device_status_set_tilt(), standing in for a real accelerometer driver.
// Only built when CONFIG_TILT_SIM is enabled.
void tilt_sim_start(void);

#ifdef __cplusplus
}
#endif

#endif

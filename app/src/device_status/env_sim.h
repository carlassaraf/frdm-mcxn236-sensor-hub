#ifndef ENV_SIM_H
#define ENV_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

// Starts a self-rescheduling work item that feeds a synthetic ppm reading
// (plus its simulated ADC voltage) into device_status_set_environment(),
// standing in for a real environment sensor driver. Only built when
// CONFIG_ENV_SIM is enabled.
void env_sim_start(void);

#ifdef __cplusplus
}
#endif

#endif

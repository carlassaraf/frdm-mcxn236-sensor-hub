#ifndef CAN_SERVICE_H
#define CAN_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Puts flexcan0 in loopback, installs the RX filters and starts the
 * periodic telemetry TX on the CAN service's own work queue
 * @return 0 on success, negative errno otherwise (also reported as
 * DEVICE_STATUS_ERROR in the can section)
 */
int can_service_start(void);

#ifdef __cplusplus
}
#endif

#endif

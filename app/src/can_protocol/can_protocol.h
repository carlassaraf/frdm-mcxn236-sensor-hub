#ifndef CAN_PROTOCOL_H
#define CAN_PROTOCOL_H

#include <zephyr/drivers/can.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sensor hub CAN protocol. Classic CAN 2.0, 11-bit standard IDs, all
 * multi-byte fields little-endian. The ID names the message (not a node) and
 * doubles as bus priority: lower ID wins arbitration.
 *
 * 0x010 CMD (host -> hub), DLC >= 1
 *   [0]    opcode   uint8   CAN_CMD_*
 *   [1]    arg      uint8   opcode-specific, 0 if unused
 *
 * 0x100 TELEM_ACCEL (hub -> bus), DLC 8
 *   [0..1] accel_x  int16   1 mg/LSB (+/-32.767 g)
 *   [2..3] accel_y  int16   1 mg/LSB
 *   [4..5] accel_z  int16   1 mg/LSB
 *   [6]    status   uint8   device_status_t
 *   [7]    seq      uint8   per-ID counter, wraps at 255
 *
 * 0x101 TELEM_ENV (hub -> bus), DLC 8
 *   [0..1] value    uint16  1 ppm/LSB
 *   [2..3] voltage  uint16  1 mV/LSB
 *   [4]    channel  uint8   ADC channel
 *   [5]    status   uint8   device_status_t
 *   [6]    reserved uint8   0
 *   [7]    seq      uint8   per-ID counter, wraps at 255
 *
 * Out-of-range values saturate to the field's limits instead of wrapping.
 */

#define CAN_ID_CMD           0x010
#define CAN_ID_TELEM_ACCEL   0x100
#define CAN_ID_TELEM_ENV     0x101

// Filter mask matching every telemetry ID in 0x100..0x10F
#define CAN_ID_TELEM_MASK    0x7F0

#define CAN_CMD_WAKE_REFRESH 0x01

#define CAN_TELEM_DLC        8

struct can_cmd {
  uint8_t opcode;
  uint8_t arg;
};

struct can_telem_accel {
  float x_g;
  float y_g;
  float z_g;
  uint8_t status;
  uint8_t seq;
};

struct can_telem_env {
  float value_ppm;
  float voltage_v;
  uint8_t channel;
  uint8_t status;
  uint8_t seq;
};

void can_protocol_encode_cmd(struct can_frame *frame, const struct can_cmd *cmd);
void can_protocol_encode_accel(struct can_frame *frame, const struct can_telem_accel *accel);
void can_protocol_encode_env(struct can_frame *frame, const struct can_telem_env *env);

/**
 * @brief Decoders validate ID, flags and DLC before touching the payload
 * @return 0 on success, -EINVAL if the frame is not that message
 */
int can_protocol_decode_cmd(const struct can_frame *frame, struct can_cmd *cmd);
int can_protocol_decode_accel(const struct can_frame *frame, struct can_telem_accel *accel);
int can_protocol_decode_env(const struct can_frame *frame, struct can_telem_env *env);

#ifdef __cplusplus
}
#endif

#endif

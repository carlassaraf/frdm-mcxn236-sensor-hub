#include "can_protocol.h"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

// Scale a physical value to an integer field, saturating instead of wrapping
static int16_t to_i16(float value, float per_lsb)
{
  float scaled = roundf(value / per_lsb);
  return (int16_t)CLAMP(scaled, (float)INT16_MIN, (float)INT16_MAX);
}

static uint16_t to_u16(float value, float per_lsb)
{
  float scaled = roundf(value / per_lsb);
  return (uint16_t)CLAMP(scaled, 0.0f, (float)UINT16_MAX);
}

static bool is_std_data_frame(const struct can_frame *frame, uint32_t id)
{
  return frame->id == id && (frame->flags & (CAN_FRAME_IDE | CAN_FRAME_RTR)) == 0;
}

void can_protocol_encode_cmd(struct can_frame *frame, const struct can_cmd *cmd)
{
  memset(frame, 0, sizeof(*frame));
  frame->id = CAN_ID_CMD;
  frame->dlc = 2;
  frame->data[0] = cmd->opcode;
  frame->data[1] = cmd->arg;
}

void can_protocol_encode_accel(struct can_frame *frame, const struct can_telem_accel *accel)
{
  memset(frame, 0, sizeof(*frame));
  frame->id = CAN_ID_TELEM_ACCEL;
  frame->dlc = CAN_TELEM_DLC;
  sys_put_le16((uint16_t)to_i16(accel->x_g, 0.001f), &frame->data[0]);
  sys_put_le16((uint16_t)to_i16(accel->y_g, 0.001f), &frame->data[2]);
  sys_put_le16((uint16_t)to_i16(accel->z_g, 0.001f), &frame->data[4]);
  frame->data[6] = accel->status;
  frame->data[7] = accel->seq;
}

void can_protocol_encode_env(struct can_frame *frame, const struct can_telem_env *env)
{
  memset(frame, 0, sizeof(*frame));
  frame->id = CAN_ID_TELEM_ENV;
  frame->dlc = CAN_TELEM_DLC;
  sys_put_le16(to_u16(env->value_ppm, 1.0f), &frame->data[0]);
  sys_put_le16(to_u16(env->voltage_v, 0.001f), &frame->data[2]);
  frame->data[4] = env->channel;
  frame->data[5] = env->status;
  frame->data[6] = 0;
  frame->data[7] = env->seq;
}

int can_protocol_decode_cmd(const struct can_frame *frame, struct can_cmd *cmd)
{
  if (!is_std_data_frame(frame, CAN_ID_CMD) || frame->dlc < 1) {
    return -EINVAL;
  }
  cmd->opcode = frame->data[0];
  cmd->arg = frame->dlc >= 2 ? frame->data[1] : 0;
  return 0;
}

int can_protocol_decode_accel(const struct can_frame *frame, struct can_telem_accel *accel)
{
  if (!is_std_data_frame(frame, CAN_ID_TELEM_ACCEL) || frame->dlc != CAN_TELEM_DLC) {
    return -EINVAL;
  }
  accel->x_g = (int16_t)sys_get_le16(&frame->data[0]) * 0.001f;
  accel->y_g = (int16_t)sys_get_le16(&frame->data[2]) * 0.001f;
  accel->z_g = (int16_t)sys_get_le16(&frame->data[4]) * 0.001f;
  accel->status = frame->data[6];
  accel->seq = frame->data[7];
  return 0;
}

int can_protocol_decode_env(const struct can_frame *frame, struct can_telem_env *env)
{
  if (!is_std_data_frame(frame, CAN_ID_TELEM_ENV) || frame->dlc != CAN_TELEM_DLC) {
    return -EINVAL;
  }
  env->value_ppm = (float)sys_get_le16(&frame->data[0]);
  env->voltage_v = sys_get_le16(&frame->data[2]) * 0.001f;
  env->channel = frame->data[4];
  env->status = frame->data[5];
  env->seq = frame->data[7];
  return 0;
}

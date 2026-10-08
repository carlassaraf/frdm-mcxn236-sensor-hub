#include "can_service.h"
#include "can_protocol.h"
#include "device_status.h"
#include "ui_manager.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(can_service, LOG_LEVEL_INF);

#if !DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(flexcan0))
#error "flexcan0 has no status okay in devicetree"
#endif

// How long can_send() may block waiting for a free TX mailbox + completion
#define CAN_TX_TIMEOUT        K_MSEC(100)
// How long to wait for a sent frame to come back through the RX filter.
// In loopback it's back within microseconds; this only bounds a broken path.
#define CAN_LOOPBACK_TIMEOUT  K_MSEC(20)

static const struct device *s_can = DEVICE_DT_GET(DT_NODELABEL(flexcan0));

K_THREAD_STACK_DEFINE(can_stack, CONFIG_CAN_SERVICE_STACK_SIZE);
static struct k_work_q s_work_q;
static const struct k_work_queue_config s_work_q_cfg = {
  .name = "can work queue",
};

static struct k_work_delayable s_tx_work;
static struct k_work s_cmd_work;

// Looped-back telemetry lands here (filled from ISR context by the driver)
CAN_MSGQ_DEFINE(s_telem_rx_msgq, 4);

static const struct can_filter s_telem_filter = {
  .id = CAN_ID_TELEM_ACCEL,
  .mask = CAN_ID_TELEM_MASK,
};

static const struct can_filter s_cmd_filter = {
  .id = CAN_ID_CMD,
  .mask = CAN_STD_ID_MASK,
};

// Only touched from the CAN work queue, so no locking needed
static uint8_t s_seq;
static uint32_t s_tx_count;
static uint32_t s_rx_count;
static bool s_loopback_ok;

static void can_tx_work_handler(struct k_work *work);
static void can_cmd_work_handler(struct k_work *work);
static void can_cmd_rx_cb(const struct device *dev, struct can_frame *frame, void *user_data);

int can_service_start(void)
{
  int err;

  if (!device_is_ready(s_can)) {
    LOG_ERR("%s not ready", s_can->name);
    err = -ENODEV;
    goto fail;
  }

  err = can_set_mode(s_can, CAN_MODE_LOOPBACK);
  if (err != 0) {
    LOG_ERR("Failed to set CAN_MODE_LOOPBACK: %d", err);
    goto fail;
  }

  // The queue must be running before the command filter is installed: its
  // callback submits to this queue
  k_work_queue_init(&s_work_q);
  k_work_queue_start(&s_work_q, can_stack, K_THREAD_STACK_SIZEOF(can_stack),
                     CONFIG_CAN_SERVICE_PRIORITY, &s_work_q_cfg);
  k_work_init_delayable(&s_tx_work, can_tx_work_handler);
  k_work_init(&s_cmd_work, can_cmd_work_handler);

  // Filters go in before can_start() so no frame is received unfiltered.
  // Both return a filter id (>= 0) on success.
  err = can_add_rx_filter_msgq(s_can, &s_telem_rx_msgq, &s_telem_filter);
  if (err < 0) {
    LOG_ERR("Failed to add telemetry RX filter: %d", err);
    goto fail;
  }

  err = can_add_rx_filter(s_can, can_cmd_rx_cb, NULL, &s_cmd_filter);
  if (err < 0) {
    LOG_ERR("Failed to add command RX filter: %d", err);
    goto fail;
  }

  err = can_start(s_can);
  if (err != 0) {
    LOG_ERR("Failed to start %s: %d", s_can->name, err);
    goto fail;
  }

  k_work_reschedule_for_queue(&s_work_q, &s_tx_work, K_MSEC(CONFIG_CAN_SERVICE_TX_INTERVAL_MS));
  LOG_INF("%s started in loopback, telemetry every %d ms", s_can->name,
          CONFIG_CAN_SERVICE_TX_INTERVAL_MS);
  return 0;

fail:
  device_status_set_can(false, 0, CONFIG_CAN_SERVICE_TX_INTERVAL_MS, 0, 0, DEVICE_STATUS_ERROR);
  return err;
}

/**
 * @brief Sends one frame and checks it comes back unchanged through the
 * telemetry RX filter
 * @return 0 if echoed intact, -EAGAIN if nothing came back, -EBADMSG if it
 * came back different, or the can_send() error
 */
static int send_and_verify(const struct can_frame *tx)
{
  struct can_frame rx;

  // Drop anything stale (a late echo, or a frame injected from the shell) so
  // the next message really is the echo of this frame
  k_msgq_purge(&s_telem_rx_msgq);

  int err = can_send(s_can, tx, CAN_TX_TIMEOUT, NULL, NULL);
  if (err != 0) {
    LOG_ERR("0x%03x: can_send failed: %d", tx->id, err);
    return err;
  }
  s_tx_count++;

  if (k_msgq_get(&s_telem_rx_msgq, &rx, CAN_LOOPBACK_TIMEOUT) != 0) {
    LOG_WRN("0x%03x: no loopback echo", tx->id);
    return -EAGAIN;
  }
  s_rx_count++;

  if (rx.id != tx->id || rx.dlc != tx->dlc ||
      memcmp(rx.data, tx->data, can_dlc_to_bytes(tx->dlc)) != 0) {
    LOG_WRN("0x%03x: loopback echo mismatch (got 0x%03x)", tx->id, rx.id);
    LOG_HEXDUMP_WRN(tx->data, can_dlc_to_bytes(tx->dlc), "sent");
    LOG_HEXDUMP_WRN(rx.data, can_dlc_to_bytes(rx.dlc), "received");
    return -EBADMSG;
  }

  LOG_HEXDUMP_DBG(rx.data, can_dlc_to_bytes(rx.dlc), "echo ok");
  return 0;
}

/**
 * @brief Work handler called every CONFIG_CAN_SERVICE_TX_INTERVAL_MS to pack
 * the latest sensor readings into telemetry frames and send them
 */
static void can_tx_work_handler(struct k_work *work)
{
  struct device_status_tilt tilt;
  struct device_status_environment env;
  device_status_get_tilt(&tilt);
  device_status_get_environment(&env);

  const struct can_telem_accel accel = {
    .x_g = tilt.x,
    .y_g = tilt.y,
    .z_g = tilt.z,
    .status = (uint8_t)tilt.status,
    .seq = s_seq,
  };
  const struct can_telem_env telem_env = {
    .value_ppm = env.value,
    .voltage_v = env.voltage,
    .channel = env.channel,
    .status = (uint8_t)env.status,
    .seq = s_seq,
  };
  s_seq++;

  struct can_frame frame;
  can_protocol_encode_accel(&frame, &accel);
  int err = send_and_verify(&frame);
  if (err == 0) {
    can_protocol_encode_env(&frame, &telem_env);
    err = send_and_verify(&frame);
  }

  // A lost/corrupted echo means the CAN path is degraded; a failed send
  // means the controller itself is in trouble
  bool loopback_ok = (err == 0);
  device_status_t status;
  if (loopback_ok) {
    status = DEVICE_STATUS_OK;
  } else if (err == -EAGAIN || err == -EBADMSG) {
    status = DEVICE_STATUS_WARN;
  } else {
    status = DEVICE_STATUS_ERROR;
  }

  if (loopback_ok != s_loopback_ok) {
    LOG_INF("Loopback %s (tx=%u rx=%u)", loopback_ok ? "OK" : "FAILED", s_tx_count, s_rx_count);
    s_loopback_ok = loopback_ok;
  }

  // Both telemetry IDs go out every cycle; the screen shows the base of the
  // telemetry range rather than flipping between them
  device_status_set_can(loopback_ok, CAN_ID_TELEM_ACCEL, CONFIG_CAN_SERVICE_TX_INTERVAL_MS,
                        s_tx_count, s_rx_count, status);

  k_work_reschedule_for_queue(&s_work_q, &s_tx_work, K_MSEC(CONFIG_CAN_SERVICE_TX_INTERVAL_MS));
}

/**
 * @brief Runs in ISR context: only decode and defer, never block here
 */
static void can_cmd_rx_cb(const struct device *dev, struct can_frame *frame, void *user_data)
{
  struct can_cmd cmd;

  if (can_protocol_decode_cmd(frame, &cmd) != 0) {
    return;
  }
  if (cmd.opcode == CAN_CMD_WAKE_REFRESH) {
    k_work_submit_to_queue(&s_work_q, &s_cmd_work);
  }
}

/**
 * @brief Wake + refresh now: wakes the display and sends telemetry right away
 * instead of waiting for the next period
 */
static void can_cmd_work_handler(struct k_work *work)
{
  LOG_INF("Wake + refresh command received");
  ui_manager_wake();
  // Same queue as the TX work, so this can't race a TX already in progress;
  // it just pulls the next one forward
  k_work_reschedule_for_queue(&s_work_q, &s_tx_work, K_NO_WAIT);
}

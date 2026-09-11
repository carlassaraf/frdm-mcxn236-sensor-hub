#ifndef MQ_H
#define MQ_H

#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

enum mq_channel {
  SENSOR_CHAN_MQ_MV = SENSOR_CHAN_VOLTAGE,
  SENSOR_CHAN_MQ_SMOKE = SENSOR_CHAN_PRIV_START,
  SENSOR_CHAN_MQ_ALCOHOL,
  SENSOR_CHAN_MQ_CO,
  SENSOR_CHAN_MQ_ALARM,
};

#ifdef __cplusplus
}
#endif

#endif
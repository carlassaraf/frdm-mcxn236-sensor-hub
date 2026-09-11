#include <math.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "mq.h"

LOG_MODULE_REGISTER(mq, CONFIG_SENSOR_LOG_LEVEL);

struct mq_config {
  const struct adc_dt_spec *adc;
  struct adc_sequence sequence;
  const struct gpio_dt_spec *digital_gpio;
  int ro_clean_air_ohms;
  int load_resistance_ohms;
  int supply_microvolts;
  const float *curve;         /* [0] = m, [1] = b in log10(ppm) = m*log10(Rs/Ro) + b */
  enum sensor_channel gas_channel;
};

struct mq_data {
  uint32_t sample;
  bool digital_out;
};

/* Driver API */

static int mq_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
  ARG_UNUSED(chan);
  const struct mq_config *config = dev->config;
  struct mq_data *data = dev->data;

  int ret = gpio_pin_get_dt(config->digital_gpio);
  if (ret < 0) {
    LOG_ERR("Failed to read DO: %d", ret);
    return ret;
  }
  data->digital_out = (bool)ret;
  return adc_read_dt(config->adc, &config->sequence);
}

/*
 * Shared raw-ADC-code -> volts conversion, used by both the ppm math and the
 * raw voltage channel below. Goes through adc_raw_to_microvolts_dt() rather
 * than a hand-rolled formula so it automatically respects whatever
 * reference/gain the devicetree channel@0 node actually configures --
 * adc_ref_internal() (tried here previously) only reports a driver's
 * ADC_REF_INTERNAL bandgap value, which adc_mcux_lpadc.c doesn't even
 * populate; it silently returned 0 regardless of the real reading.
 */
static int mq_voltage(const struct mq_config *config, const struct mq_data *data, float *v_out)
{
  int32_t uv = data->sample;
  int ret = adc_raw_to_microvolts_dt(config->adc, &uv);
  if (ret != 0) {
    return ret;
  }

  *v_out = uv / 1000000.0f;
  return 0;
}

/*
 * MQ datasheets give the Rs/Ro-vs-ppm curve as a straight line in log-log
 * space: log10(ppm) = m*log10(Rs/Ro) + b, with (m, b) read off the
 * datasheet's graph for the target gas. Rs itself isn't measured directly --
 * it's derived from the voltage divider the sensor forms with its onboard
 * load resistor RL: the ADC reads Vout across RL, and Rs = RL*(Vc-Vout)/Vout.
 */
static int mq_ppm(const struct mq_config *config, const struct mq_data *data,
                   struct sensor_value *val)
{
  float v_out;
  int ret = mq_voltage(config, data, &v_out);
  if (ret != 0) {
    return ret;
  }

  if (v_out <= 0.0f) {
    /* Sensor disconnected or RL wired backwards -- Rs would be infinite/negative. */
    return -EIO;
  }

  float v_supply = config->supply_microvolts / 1000000.0f;
  float rs = config->load_resistance_ohms * (v_supply - v_out) / v_out;
  float ratio = rs / (float)config->ro_clean_air_ohms;
  if (ratio <= 0.0f) {
    return -EIO;
  }

  float log_ppm = config->curve[0] * log10f(ratio) + config->curve[1];

  return sensor_value_from_float(val, powf(10.0f, log_ppm));
}

static int mq_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val)
{
  const struct mq_config *config = dev->config;
  const struct mq_data *data = dev->data;

  if (chan == (enum sensor_channel)SENSOR_CHAN_MQ_MV) {
    float v_out;
    int ret = mq_voltage(config, data, &v_out);
    if (ret != 0) {
      return ret;
    }
    return sensor_value_from_float(val, v_out);
  }
  if (chan == (enum sensor_channel)SENSOR_CHAN_MQ_ALARM) {
    val->val1 = data->digital_out ? 1 : 0;
    val->val2 = 0;
    return 0;
  }
  if (chan == (enum sensor_channel)config->gas_channel) {
    return mq_ppm(config, data, val);
  }
  return -ENOTSUP;
}

static int mq_init(const struct device *dev)
{
  const struct mq_config *config = dev->config;

  if (!adc_is_ready_dt(config->adc)) {
    LOG_ERR("ADC is not ready");
    return -ENODEV;
  }

  int ret = adc_channel_setup_dt(config->adc);
  if (ret != 0) {
    LOG_ERR("%s setup failed: %d", config->adc->dev->name, ret);
    return -ENODEV;
  }

  if (!gpio_is_ready_dt(config->digital_gpio)) {
    LOG_ERR("GPIO for DO pin not available");
    return -ENODEV;
  }

  ret = gpio_pin_configure_dt(config->digital_gpio, GPIO_INPUT);
  if (ret != 0) {
    LOG_ERR("Failed to configure GPIO");
    return -ENODEV;
  }

  return 0;
}

static DEVICE_API(sensor, mq_driver_api) = {
  .sample_fetch = mq_sample_fetch,
  .channel_get = mq_channel_get
};

/*
 * One instantiation macro shared by all three compatibles. Everything that
 * differs between an MQ-2, MQ-3 and MQ-7 node -- which ADC/GPIO pins, which
 * physical RL/Ro, and which gas channel + curve to report -- is data read
 * out of devicetree or passed in as a macro argument; the macro body itself
 * doesn't know or care which compatible it's expanding for.
 */
#define MQ_INIT(inst, compat, gas_chan, curve_tbl)                                  \
  static struct mq_data mq_data_##compat##_##inst;                                  \
  static const struct adc_dt_spec mq_adc_##compat##_##inst = ADC_DT_SPEC_INST_GET(inst); \
  static const struct gpio_dt_spec mq_digital_gpio_##compat##_##inst =              \
      GPIO_DT_SPEC_INST_GET(inst, do_gpios);                                        \
  static const struct mq_config mq_config_##compat##_##inst = {                     \
      .adc = &mq_adc_##compat##_##inst,                                             \
      .sequence =                                                                   \
          {                                                                         \
              .options = NULL,                                                      \
              .channels = BIT(mq_adc_##compat##_##inst.channel_id),                 \
              .buffer = &mq_data_##compat##_##inst.sample,                          \
              .buffer_size = sizeof(mq_data_##compat##_##inst.sample),              \
              .resolution = mq_adc_##compat##_##inst.resolution,                    \
              .oversampling = mq_adc_##compat##_##inst.oversampling,                \
              .calibrate = false,                                                   \
          },                                                                        \
      .digital_gpio = &mq_digital_gpio_##compat##_##inst,                           \
      .load_resistance_ohms = DT_INST_PROP(inst, load_resistance_ohms),             \
      .ro_clean_air_ohms = DT_INST_PROP(inst, ro_clean_air_ohms),                   \
      .supply_microvolts = DT_INST_PROP(inst, power_supply_microvolts),             \
      .curve = (curve_tbl),                                                         \
      .gas_channel = (gas_chan),                                                    \
  };                                                                                \
  SENSOR_DEVICE_DT_INST_DEFINE(inst, mq_init, NULL, &mq_data_##compat##_##inst,     \
                                &mq_config_##compat##_##inst, POST_KERNEL,          \
                                CONFIG_SENSOR_INIT_PRIORITY, &mq_driver_api);

/*
 * Curve constants below are a 2-point log-log fit -- (m, b) for
 * log10(ppm) = m*log10(Rs/Ro) + b -- derived from two (Rs/Ro, ppm) points
 * read off each datasheet's "Typical Sensitivity Curve" graph for the
 * target gas: m = (y2-y1)/(x2-x1), b = y1 - m*x1, with x=log10(Rs/Ro) and
 * y=log10(ppm). Accuracy is bounded by how precisely those two points were
 * read off the graph -- a third point as a spot-check is cheap insurance.
 */
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT winsen_mq2
/* Smoke line -- points (0.042, 5000ppm), (0.026, 10000ppm) */
static const float mq2_curve[] = {-1.445f, 1.709f};
DT_INST_FOREACH_STATUS_OKAY_VARGS(MQ_INIT, DT_DRV_COMPAT, SENSOR_CHAN_MQ_SMOKE, mq2_curve)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT winsen_mq3
/* Alcohol (C2H5OH) line -- points (0.085, 100ppm), (0.0475, 200ppm) */
static const float mq3_curve[] = {-1.191f, 0.725f};
DT_INST_FOREACH_STATUS_OKAY_VARGS(MQ_INIT, DT_DRV_COMPAT, SENSOR_CHAN_MQ_ALCOHOL, mq3_curve)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT winsen_mq7
/* CO line -- points (0.065, 100ppm), (0.02, 1000ppm) */
static const float mq7_curve[] = {-1.954f, -0.319f};
DT_INST_FOREACH_STATUS_OKAY_VARGS(MQ_INIT, DT_DRV_COMPAT, SENSOR_CHAN_MQ_CO, mq7_curve)
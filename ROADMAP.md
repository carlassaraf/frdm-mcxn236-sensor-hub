# FRDM-MCXN236 Sensor Hub — Roadmap

A CAN-aware environmental/motion monitor with an LVGL touchscreen dashboard,
built on the NXP FRDM-MCXN236 (Zephyr RTOS).

## Hardware

- FRDM-MCXN236 dev board
- ILI9341 2.4" parallel LCD (MCUFRIEND-style Arduino Uno shield, 8-bit 8080 bus)
- On-board FXLS8974 accelerometer (I3C, upstream Zephyr driver)
- A sensor with **no existing Zephyr driver** for the from-scratch driver exercise
  (default recommendation: **MQ-135 analog air-quality/gas sensor** — plain ADC read
  + datasheet conversion curve, no upstream Zephyr binding, cheap and common locally.
  Note: HC-SR04 and Hall-effect pulse flow meters — both earlier candidates — turned
  out to already have upstream drivers as of 2024/2026, so they're excluded. Always
  re-check `zephyr/drivers/sensor` before committing to a part; upstream coverage
  keeps growing.)
- No second CAN node for now — FlexCAN loopback only

## Architecture summary

- Sensor-sampling thread → writes a mutex-protected `sensor_snapshot` struct
- CAN TX thread → reads snapshot under mutex, sends periodic CAN-FD telemetry frames (loopback)
- Button/accelerometer GPIO ISR → `k_work` (workqueue) → wakes display, triggers immediate telemetry
- LVGL workqueue (built into Zephyr's LVGL module) → reads snapshot under mutex, refreshes UI
- App layer in C++; drivers and Zephyr subsystem glue stay in C
- MCUboot-signed image (chain-of-trust secure boot)
- Sleep-by-default / wake-on-event power management

---

## 0. Toolchain & workspace bring-up

**Requisites:** working `west` workspace, Zephyr SDK, a Zephyr revision that includes the `frdm_mcxn236` board (already maintained upstream).

- [ ] Confirm the board is available: `west boards | grep frdm_mcxn236`
- [ ] Create the application skeleton (`app/`, `CMakeLists.txt`, `prj.conf`, `boards/frdm_mcxn236.overlay`)
- [ ] Build & flash `hello_world` to confirm board bring-up:
      `west build -b frdm_mcxn236/mcxn236 samples/hello_world && west flash`
- [ ] Confirm serial console on J10 (115200 8N1)

## 1. Display bring-up — ILI9341 over GPIO-bitbang MIPI-DBI

**Requisites:**
- `zephyr,mipi-dbi-bitbang` (GPIO bit-bang MIPI DBI Type B / 8080 parallel) — works over any GPIOs, no dedicated LCD header needed
- `ilitek,ili9341` display driver — already upstream, 240×320, DT-configurable to 8080 parallel mode
- Devicetree overlay referencing the board's `arduino_header` nexus node (not raw SoC GPIOs)

**Verified pin mapping** (MCUFRIEND standard pinout → `arduino-header-r3` nexus index):

| Shield signal | Arduino pin | `arduino_header` index |
|---|---|---|
| LCD_D0 | D8  | 14 |
| LCD_D1 | D9  | 15 |
| LCD_D2 | D2  | 8  |
| LCD_D3 | D3  | 9  |
| LCD_D4 | D4  | 10 |
| LCD_D5 | D5  | 11 |
| LCD_D6 | D6  | 12 |
| LCD_D7 | D7  | 13 |
| CS     | A3  | 3  |
| CD/DC  | A2  | 2  |
| WR     | A1  | 1  |
| RD     | A0  | 0  |
| RST    | A4  | 4  |

- [ ] Write `boards/frdm_mcxn236.overlay`: `mipi_dbi` bitbang node (`data-gpios`, `cs-gpios`, `wr-gpios`, `rd-gpios`, `dc-gpios`, `reset-gpios`) + child `ili9341@0` node, `mipi-mode = "MIPI_DBI_MODE_8080_BUS_8_BIT"`, `width = <240>`, `height = <320>`
- [ ] Set `zephyr,display` chosen node to the ILI9341 instance
- [ ] Enable `CONFIG_DISPLAY=y`, `CONFIG_MIPI_DBI=y`, `CONFIG_ILI9341=y`
- [ ] Run `samples/display` (checkerboard/pattern test) to confirm bring-up before touching LVGL
- [ ] Check refresh performance; if sluggish, confirm the driver's same-GPIO-port fast path (data LUT) is active for the 8-bit bus

## 2. LVGL + SquareLine Studio UI

**Requisites:** Zephyr's LVGL module (currently v9.x) enabled; SquareLine Studio project set to LVGL v9, 240×320, RGB565.

- [ ] Enable `CONFIG_LVGL=y`, 16-bit color depth, `CONFIG_LV_Z_RUN_LVGL_ON_WORKQUEUE=y`
- [ ] Build & flash a built-in LVGL demo (`samples/modules/lvgl/demos`) to confirm the full pipeline first
- [ ] Design the dashboard in SquareLine Studio (project settings: LVGL v9.x, 240×320, RGB565)
- [ ] Export the UI-only project, copy generated `ui/` sources into `src/ui/`
- [ ] Call `ui_init()` right after `lvgl_init()` runs in `main()`
- [ ] Wrap every `lv_obj_*` call made from the sensor/CAN threads in `lvgl_lock()` / `lvgl_unlock()`

## 3. Custom sensor driver (from scratch)

**Requisites:** a sensor genuinely absent from `zephyr/drivers/sensor` (verify with a grep — do not assume, upstream coverage keeps growing). Default pick: **MQ-135** analog air-quality sensor (or MQ-2/MQ-3/MQ-7 — same driver shape). Pure ADC input + a datasheet Rs/Ro-vs-ppm conversion curve; the upstream `sensor/omron/d6f` flow driver is a good structural reference for an ADC-based sensor driver with a polynomial conversion.

- [ ] Confirm the chosen sensor has no upstream driver (`grep -ri mq13 zephyr/drivers/sensor` etc. — re-check, don't trust this roadmap's snapshot)
- [ ] Wire the sensor's analog output to an MCXN236 ADC-capable pin on the Arduino header (LPADC), plus its heater supply per the datasheet warm-up requirement
- [ ] Create the out-of-tree driver: `dts/bindings/sensor/*.yaml` binding (`io-channels` property for the ADC), `drivers/sensor/<name>/<name>.c`, `Kconfig`, `CMakeLists.txt`
- [ ] Implement `sample_fetch` (ADC read via the `adc_dt_spec`/`adc_sequence` API) and `channel_get` (apply the Rs/Ro curve, report on `SENSOR_CHAN_GAS_RES` or a custom channel)
- [ ] Account for the sensor's warm-up time (MQ-series typically need tens of seconds to minutes before stable readings)
- [ ] Wire the driver into the build (`ZEPHYR_EXTRA_MODULES` or an app-local `drivers/` tree)
- [ ] Validate standalone with a simple polling sample before integrating into the main app

## 4. Threading / concurrency architecture

- [ ] Define shared `struct sensor_snapshot` (accel xyz + distance/whatever the custom sensor reports), protected by a `k_mutex`
- [ ] Sensor-sampling thread: periodic `k_thread` polling the FXLS8974 accelerometer + the custom sensor, writes snapshot under mutex
- [ ] CAN TX thread: reads snapshot under mutex, packs into a CAN-FD frame, sends periodically
- [ ] Button/accelerometer GPIO ISR → `k_work` on a workqueue for debounced handling (wake display, trigger immediate telemetry)
- [ ] LVGL workqueue reads the snapshot under mutex to refresh widgets
- [ ] Keep every mutex-held critical section short — no I2C/CAN transactions while holding the lock

## 5. CAN (FlexCAN, loopback only)

- [ ] Enable `CONFIG_CAN=y`; confirm `can0` node status in the overlay
- [ ] Configure `CAN_MODE_LOOPBACK` at init
- [ ] Define a simple telemetry frame layout (accel + distance) and one command frame ID (e.g. "wake + refresh now")
- [ ] TX thread sends telemetry every N ms; RX filter/callback decodes command frames and submits the wake `k_work`
- [ ] Verify TX == RX end-to-end in loopback via shell/log
- [ ] *(Not now, documented as a future extension)* real bus access via USB-CAN adapter or a second board

## 6. C++ application layer

- [ ] Enable `CONFIG_CPP=y`, `CONFIG_CPLUSPLUS=y`, `CONFIG_STD_CPP17=y`, `CONFIG_LIB_CPLUSPLUS=y`
- [ ] Wrap the sensor snapshot in a small C++ class (RAII lock guard around the `k_mutex`)
- [ ] Wrap CAN telemetry packing/parsing in a C++ class
- [ ] Keep a clean `extern "C"` boundary between LVGL/driver C APIs and the C++ app layer

## 7. Secure boot (MCUboot)

**Requisites:** sysbuild, `imgtool`, your own ECDSA keypair for anything beyond initial bring-up.

- [ ] Build with sysbuild: `west build --sysbuild -- -DSB_CONFIG_BOOTLOADER_MCUBOOT=y`
- [ ] Confirm the default MCUboot dev key works end-to-end first
- [ ] Generate your own keypair (`imgtool keygen`), point `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` at it, keep the private key out of the repo
- [ ] Negative test: confirm an unsigned/corrupted image is rejected by MCUboot
- [ ] *(Stretch)* wire up `mcumgr`/SMP server to demonstrate a signed firmware update over UART/USB
- [ ] Document explicitly: no TF-M/TrustZone-M secure/non-secure split exists for this SoC in upstream Zephyr yet — this is MCUboot chain-of-trust, not isolated worlds

## 8. Low power

**Requisites:** `CONFIG_PM=y`, a wakeup source (button or accelerometer interrupt via the WUU, or an LPTMR/RTC alarm).

- [ ] Enable `CONFIG_PM=y`, `CONFIG_PM_DEVICE=y`
- [ ] Start from `tests/subsys/pm/power_mgmt_soc/boards/frdm_mcxn236.{conf,overlay}` as a reference
- [ ] Configure the wakeup source as a `wakeup-source` GPIO through the WUU
- [ ] Implement the sleep/wake cycle: deep-sleep (`PM_STATE_STANDBY`) by default → wake on event → refresh display + send CAN telemetry → sleep again
- [ ] If a multimeter/power profiler is available, measure actual current draw (reference figures for this SoC family: ~8 µA standby, ~196 µA suspend-to-idle, ~13.5 mA runtime-idle)
- [ ] Confirm the display driver handles blanking/backlight power-down cleanly across sleep cycles

## 9. Stretch goals (optional, not required for v1)

- [ ] Real CAN bus test with a USB-CAN adapter or a second board
- [ ] MCUboot image encryption
- [ ] Contribute the from-scratch sensor driver upstream to Zephyr

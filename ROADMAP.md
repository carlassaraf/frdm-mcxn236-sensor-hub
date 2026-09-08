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

- Sensor-sampling thread → writes a mutex-protected `sensor_snapshot` struct (raw readings, consumed by CAN)
- CAN TX thread → reads snapshot under mutex, sends periodic CAN-FD telemetry frames (loopback)
- Input service: GPIO interrupt (not polling) → `k_work` debounce → requests a screen change
- A dedicated **UI Manager** owns a dedicated LVGL thread and is the *only* code that ever touches
  an `lv_obj_t*` — no built-in `lvgl_lock()`/`lvgl_unlock()` exists in mainline, so this ownership
  boundary is what replaces it. Sensor/CAN/input threads never touch LVGL directly; they push
  UI-relevant derived state into a shared, mutex-protected `device_status` struct and wake the UI
  Manager via a coalescing semaphore. Screens are lazily created/destroyed on switch (only the
  active screen's LVGL objects exist at a time). Full design: **[ARCHITECTURE.md](ARCHITECTURE.md)**.
- App layer in C++; drivers and Zephyr subsystem glue stay in C
- MCUboot-signed image (chain-of-trust secure boot)
- Sleep-by-default / wake-on-event power management

---

## 0. Toolchain & workspace bring-up

**Requisites:** working `west` workspace, Zephyr SDK, a Zephyr revision that includes the `frdm_mcxn236` board (already maintained upstream).

- [x] Confirm the board is available: `west boards | grep frdm_mcxn236`
- [x] Create the application skeleton (`app/`, `CMakeLists.txt`, `prj.conf`, `boards/frdm_mcxn236.overlay`)
- [x] Build & flash `hello_world` to confirm board bring-up:
      `west build -b frdm_mcxn236/mcxn236 samples/hello_world && west flash`
- [x] Confirm serial console on J10 (115200 8N1)

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

- [x] Write `boards/frdm_mcxn236.overlay`: `mipi_dbi` bitbang node (`data-gpios`, `cs-gpios`, `wr-gpios`, `rd-gpios`, `dc-gpios`, `reset-gpios`) + child `ili9341@0` node, `mipi-mode = "MIPI_DBI_MODE_8080_BUS_8_BIT"`, `width = <240>`, `height = <320>`
- [x] Set `zephyr,display` chosen node to the ILI9341 instance
- [x] Enable `CONFIG_DISPLAY=y`, `CONFIG_MIPI_DBI=y`
- [x] Run `samples/drivers/display` (checkerboard/pattern test) to confirm bring-up before touching LVGL
- [x] Check refresh performance; if sluggish, confirm the driver's same-GPIO-port fast path (data LUT) is active for the 8-bit bus

> **Checked 2026-07-23:** it isn't. `mipi_dbi_bitbang.c` only enables the LUT when all 8 `data-gpios` sit on one GPIO port; ours span three (`gpio0`: DB0/DB4/DB7, `gpio2`: DB2/DB5, `gpio3`: DB1/DB3/DB6), so `single_port` is false and every byte costs 8 individual `gpio_pin_set_dt()` calls instead of one `gpio_port_set_masked()`. Verify via `CONFIG_MIPI_DBI_LOG_LEVEL_DBG=y` — no "LUT optimization enabled" line at boot confirms the slow path. Only fix is rewiring all 8 data lines onto a single port; otherwise accept the slower bitbang path.

## 2. LVGL + SquareLine Studio UI

**Requisites:** Zephyr's LVGL module (currently v9.x) enabled; SquareLine Studio project set to LVGL v9, 240×320, RGB565.

- [x] Enable `CONFIG_LVGL=y`, 16-bit color depth (`CONFIG_LV_COLOR_DEPTH_16=y`, `CONFIG_LV_COLOR_16_SWAP=y`)
- [x] Build a built-in LVGL demo (`../deps/zephyr/samples/modules/lvgl/demos`) to confirm the full
      pipeline — built clean 2026-07-23 (music demo, 90.55% flash / 54.55% RAM):
```sh
west build -b frdm_mcxn236 ../deps/zephyr/samples/modules/lvgl/demos -p -- \
  -DEXTRA_DTC_OVERLAY_FILE=$(pwd)/app/boards/frdm_mcxn236.overlay \
  -DEXTRA_CONF_FILE=$(pwd)/app/prj.conf
```
- [x] Design the dashboard in SquareLine Studio (project settings: LVGL v9.x, 240×320, RGB565)
- [x] Export the UI-only project, copy generated `ui/` sources into `src/ui/`
- [x] Call `ui_init()` right after LVGL's own `SYS_INIT`-driven init runs
- [x] Design the threaded UI ownership model (dedicated LVGL thread, shared `device_status`,
      lazy screen load/destroy) — see **[ARCHITECTURE.md](ARCHITECTURE.md)**
- [x] Implement the **UI Manager**: dedicated LVGL thread owning all `lv_obj_t*` access, the
      screen table (`init`/`destroy`/`apply` per screen), and the lazy load/destroy + hydrate
      sequencing (reusing the generated lazy-init screen-change helper and the generated
      per-screen `destroy()`'s existing pointer-nulling — don't reimplement either)
- [x] Implement the shared `device_status` struct + its `k_mutex`, plus the coalescing binary
      semaphore that wakes the UI Manager thread on change (see ARCHITECTURE.md's
      synchronization model — no message queue, latest-value-wins everywhere)
- [ ] Implement one adapter module per screen translating `device_status` → that screen's widgets;
      keep these in new hand-written files (`ui_adapter_<screen>.c`), never inside the generated
      `screens/*.c`
  - [x] `ui_adapter_splash`: relocate the existing version-string `postinit` out of `ui_manager.c`
        (no `device_status` needed)
  - [x] `ui_adapter_overview`: relocate the existing uptime `step`; wire `overall_status` → the
        hero text/color, and `env_status`/`can_status`/                         `tilt_status` → the three status rows
        (text + okg/wrn/err color via `ui_object_set_themeable_style_property`, same helper the
        generated init already uses)
  - [x] `ui_adapter_tilt`: wire `tilt_x/y/z` → the 3 axis labels + range-sliders
  - [x] `ui_adapter_environment`: wire `env_value/unit/sensor_name/channel/voltage/status` → the
        6 hero/detail widgets
  - [x] `ui_adapter_can`: wire `can_loopback_ok/frame_id/tx_interval_ms/tx_count` → the 4 widgets
        (settle the rx-count gap below first)
  - [x] `ui_adapter_power`: deliberately no adapter/`step`. `display_sleeping` moved from a
        UI-Manager-local static into a `device_status` field (SW2's handler is still its sole
        writer), but the sleep overlay lives on `lv_layer_top()`, above every screen, and both
        its visibility and any screen redraw happen in the same
        `lvgl_thread()` iteration before the single `lv_timer_handler()` flush — so the hero/
        instructions text is only ever visible while awake, and SquareLine's static text
        ("AWAKE" / "Push SW2 to sleep") is already correct in that state by construction. A
        `step` that set "SLEEPING" would be covered by the overlay in the same tick it ran,
        i.e. dead code.
  - [x] Stop `ui_manager.c` itself from reaching into screen headers directly — today
        `scrSplash_postinit`/`scrOverview_postinit`/`scrOverview_step` are defined inline in
        `ui_manager.c` and `#include "ui.h"` directly; that logic belongs in the adapters above,
        per ARCHITECTURE.md's "adapters are the only other code allowed to reach into generated
        screen headers" rule
  - [ ] Wire the LVGL thread loop to actually call `device_status_wait()` with a clamped
        floor/ceiling timeout instead of the current unconditional `k_msleep(10)` — the
        coalescing-semaphore design ARCHITECTURE.md describes is defined in `device_status.c`
        but never called anywhere in `ui_manager.c` yet

> **Reviewed 2026-08-04:** screen-by-screen adapter scope above; two open questions to settle
> before wiring rather than guessing:
> 1. `device_status` has no `can_rx_count` field/setter, but the Can screen's static label reads
>    "TX / RX count" and its placeholder shows a TX/RX pair ("348 / 348"). Either add
>    `can_rx_count` (matches the existing "one setter per producer" pattern — loopback TX==RX
>    verification is already a checklist item below) or simplify the widget to TX-only.

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

**See [ARCHITECTURE.md](ARCHITECTURE.md) for the full design** (component map, synchronization
model, screen lifecycle, thread priority rationale). Summary of the concrete build steps:

- [ ] Define shared `struct sensor_snapshot` (accel xyz + custom sensor reading), protected by its
      own `k_mutex` — raw readings, consumed by the CAN service for telemetry packing
- [ ] Define the shared `device_status` struct (UI-facing *derived* fields: status enums, tilt
      xyz, env value/unit/status, CAN counters) per ARCHITECTURE.md, with its own `k_mutex` +
      coalescing binary semaphore — separate from `sensor_snapshot`, owned by the UI Manager
- [ ] Sensor-sampling thread: periodic `k_thread` polling the FXLS8974 accelerometer + the custom
      sensor; writes `sensor_snapshot` under its mutex, computes derived status once, and pushes
      it into `device_status` via the UI Manager's setters
- [ ] CAN TX thread: reads `sensor_snapshot` under its mutex, packs into a CAN-FD frame, sends
      periodically, and pushes CAN status/counters into `device_status`
- [ ] Input service: button GPIO interrupt (not polling) → `k_work` debounce → requests a screen
      change through the UI Manager's navigation API (atomic "requested screen" + wake semaphore)
      — never touches `lv_obj_t*` or `device_status` directly
- [ ] UI Manager thread reads `device_status` under its mutex to refresh the active screen's widgets
- [ ] Keep every mutex-held critical section short — no I2C/CAN/display-driver calls while holding
      either lock

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

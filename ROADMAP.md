# FRDM-MCXN236 Sensor Hub — Roadmap

A CAN-aware environmental/motion monitor with an LVGL touchscreen dashboard,
built on the NXP FRDM-MCXN236 (Zephyr RTOS).

## Hardware

- FRDM-MCXN236 dev board
- ILI9341 2.4" parallel LCD (MCUFRIEND-style Arduino Uno shield, 8-bit 8080 bus)
- On-board FXLS8974 accelerometer (I3C, upstream Zephyr driver)
- A sensor with **no existing Zephyr driver** for the from-scratch driver exercise —
  settled on the **Winsen MQ-2 / MQ-3 / MQ-7 breakout modules** (smoke-LPG, alcohol,
  CO respectively). Each module exposes both an analog output (buffered Rs divider
  voltage, `A0`) and a comparator digital output (`D0`, trips against an on-board
  potentiometer threshold) — no upstream Zephyr binding for any of them. Note:
  HC-SR04 and Hall-effect pulse flow meters — both earlier candidates — turned out to
  already have upstream drivers as of 2024/2026, so they're excluded. Always re-check
  `zephyr/drivers/sensor` before committing to a part; upstream coverage keeps growing.
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
- [x] Implement one adapter module per screen translating `device_status` → that screen's widgets;
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
  - [x] Wire the LVGL thread loop to actually call `device_status_wait()` with a clamped
        floor/ceiling timeout instead of the current unconditional `k_msleep(10)` — the
        coalescing-semaphore design ARCHITECTURE.md describes is defined in `device_status.c`
        but never called anywhere in `ui_manager.c` yet

> **Reviewed 2026-08-04:** screen-by-screen adapter scope above; two open questions to settle
> before wiring rather than guessing:
> 1. `device_status` has no `can_rx_count` field/setter, but the Can screen's static label reads
>    "TX / RX count" and its placeholder shows a TX/RX pair ("348 / 348"). Either add
>    `can_rx_count` (matches the existing "one setter per producer" pattern — loopback TX==RX
>    verification is already a checklist item below) or simplify the widget to TX-only.

## 3. Custom sensor driver (from scratch) — MQ-2 / MQ-3 / MQ-7

**Requisites:** a sensor genuinely absent from `zephyr/drivers/sensor` (verify with a grep —
do not assume, upstream coverage keeps growing). One driver source handles all three
Winsen modules via three devicetree compatibles (`winsen,mq2`/`winsen,mq3`/`winsen,mq7` —
`winsen` is already a registered vendor prefix in `dts/bindings/vendor-prefixes.txt`),
each instance carrying its own curve constants — same multi-compatible shape as the
upstream `sensor/omron/d6f` flow driver (good structural reference for the ADC
scaffolding: `adc_dt_spec`, `adc_sequence`, `DT_INST_FOREACH_STATUS_OKAY_VARGS` fanning
one `_INIT` macro out per compatible). The **math differs from d6f**, though: MQ
datasheets give Rs/Ro vs ppm as a straight line in *log-log* space
(`log10(ppm) = m·log10(Rs/Ro) + b`, one `(m, b)` pair per gas curve on the datasheet
graph), not a polynomial in voltage — compute `Rs` from the measured voltage and a known
load resistor, then `Rs/Ro`, then invert the log-log line, per channel below.

Each module also has a **digital comparator output (D0)** in addition to the analog
one (A0) — this repo's `env_sim.c` already anticipates only the analog half
(`device_status_set_environment(value, voltage, status)`); D0 needs its own path.

### 3.1 Files to create

```
app/
├── dts/bindings/sensor/
│   ├── winsen,mq-common.yaml   # shared props: io-channels, load resistor, ro,
│   │                           #   digital-gpios, warmup, digital-debounce
│   ├── winsen,mq2.yaml         # compatible: "winsen,mq2", includes mq-common
│   ├── winsen,mq3.yaml         # compatible: "winsen,mq3", includes mq-common
│   └── winsen,mq7.yaml         # compatible: "winsen,mq7", includes mq-common
├── drivers/sensor/mq/
│   ├── CMakeLists.txt          # zephyr_library() + zephyr_library_sources(mq.c)
│   ├── Kconfig                 # config MQ, depends on DT_HAS_WINSEN_MQ{2,3,7}_ENABLED
│   ├── mq.h                    # shared config/data structs, per-variant curve table
│   └── mq.c                    # sample_fetch/channel_get/init, one _INIT macro,
│                               #   DT_DRV_COMPAT fanned out 3x like d6f.c
├── boards/frdm_mcxn236.overlay # add the mq2/mq3/mq7 DT node(s) + any extra pinctrl
├── Kconfig                     # rsource "drivers/sensor/mq/Kconfig"
├── CMakeLists.txt              # add_subdirectory(drivers/sensor/mq)
└── prj.conf                    # CONFIG_MQ=y (and CONFIG_ADC=y, pulled in by select)
```

`app/` is already in Zephyr's `DTS_ROOT` (Zephyr's `pre_dt.cmake` adds
`APPLICATION_SOURCE_DIR` automatically), so `app/dts/bindings/sensor/*.yaml` is picked
up with zero extra config — **no `ZEPHYR_EXTRA_MODULES`/`module.yml` needed**, this can
just be a plain library subdirectory wired into `app/CMakeLists.txt`/`app/Kconfig`
exactly the way `src/device_status` already is.

### 3.2 Analog path (A0)

**Pin decision:** `P4_0` (ADC0 channel `A0`) for analog, `P4_1` for digital — both live on
the `J5` connector next to 5V/GND rather than the Arduino header. Settled after ruling out
two other candidates: `P1_16` (`ADC1_A16`) is out — it's the same net as the onboard
`&i3c1` SDA line (`pinmux_i3c1`, already `status = "okay"` for the onboard FXLS8974
accelerometer) and also Arduino `D14`; re-muxing it would break the accelerometer. `P4_0`/
`P4_1` needed `&flexcomm2_lpi2c2` (I2C2, board-default `status = "okay"`, meant for the
unpopulated DA7212 codec / touch-panel / camera-connector header that likely *is* `J5`)
freed first, but nothing in this app instantiates a device on that bus, so it's safe to
reclaim.

- [x] Confirm the chosen sensors have no upstream driver (`grep -ri "mq2\|mq3\|mq7" zephyr/drivers/sensor` — re-check, don't trust this roadmap's snapshot)
- [x] Pick the analog/digital pins and free them from their board-default peripheral (see
      decision above) — **first pass in the overlay has two open problems, re-check before
      relying on it:**
      1. `&pinmux_flexcomm2_i2c { status = "disabled"; };` disables the *pinctrl group node*,
         not the *consumer*. Status on a plain pin-config node isn't something Zephyr's
         pinctrl codegen checks — the actual peripheral, `&flexcomm2_lpi2c2`, is still
         `status = "okay"` from the board dts and will still apply that group's mux on init.
         This is very likely a no-op; disable `&flexcomm2_lpi2c2` itself instead.
      2. `&lpadc0`'s `pinctrl-0` was never updated to include the new `pinmux_lpadc0_mq`
         group — as written, that group is defined but unreferenced, so `P4_0` never
         actually gets muxed to `ADC0_A0`. Needs `pinctrl-0 = <&pinmux_lpadc0>,
         <&pinmux_lpadc0_mq>;` added to the `&lpadc0` override.

      Verify both with the generated devicetree (`build/zephyr/zephyr.dts`, or
      `west build -t pinctrl` output) — confirm `flexcomm2_lpi2c2`'s status and `lpadc0`'s
      `pinctrl-0` list actually show what's intended before wiring up hardware.
- [x] Wire the heater supply (5V) per the datasheet warm-up requirement
- [x] Write the devicetree binding: `io-channels` (phandle-array to `&lpadc0`),
      `load-resistance-ohms` (the module's RL, fixed by its onboard resistor — check the
      specific breakout's silkscreen/schematic, common values are 1k–10kΩ, don't assume),
      and `ro-clean-air-ohms` — see calibration note below for why this is a DT property
      rather than a compile-time constant
- [x] Implement `sample_fetch` (ADC read via `adc_dt_spec`/`adc_sequence`, same shape as
      `d6f_sample_fetch`) and `channel_get` reporting `SENSOR_CHAN_GAS_RES` (raw `Rs/Ro`
      ratio) and a custom channel for ppm (`SENSOR_CHAN_PRIV_START`-based enum in `mq.h`,
      one per gas: smoke/LPG for MQ-2, alcohol for MQ-3, CO for MQ-7)
- [x] Per-variant curve constants: a small table of `(m, b)` log-log coefficients keyed by
      `DT_DRV_COMPAT`, mirroring how `d6f.c` keeps a separate polynomial array per
      compatible — pull the actual numbers off each datasheet's Rs/Ro-vs-ppm graph, not
      off a random blog post (several widely-copied MQ-2 "Arduino library" curve-fit
      constants online are wrong)
- [x] **Ro calibration is the easy-to-skip step that silently wrecks accuracy**: Ro (the
      sensor's own Rs in clean, known-good air) varies per physical unit and drifts with
      age — it is *not* a datasheet constant, only the clean-air `Rs/Ro` ratio is. Options,
      pick one deliberately rather than defaulting to "hardcode a number":
      - Fixed `ro-clean-air-ohms` DT property, measured once per physical board in known
        clean air and written into the overlay (simplest, fine for this exercise, degrades
        as the sensor ages)
      - A one-shot calibration routine in `init()` or a shell command that samples for the
        warm-up period and derives `Ro` from the known clean-air ratio, only re-run
        manually (more correct, more code)

### 3.3 Digital path (D0)

- [ ] Wire D0 to `P4_1` as a plain GPIO input (`digital-gpios = <&gpio4 1 ...>;` in the
      binding) — this is a comparator output, not ADC: it's a binary "above/below the pot
      threshold" flag, not a proportional reading. No pinctrl group needed for this one: once
      `&flexcomm2_lpi2c2` is actually disabled (see 3.2's open problem #1), the pin reverts
      to its hardware-reset GPIO-capable mux with nothing else claiming it
- [ ] For structure, the FXLS8974 accelerometer driver already used on this exact board
      (`drivers/sensor/nxp/fxls8974/fxls8974_trigger.c`) is a good local reference for the
      GPIO-interrupt pattern: `gpio_pin_interrupt_configure_dt`, `gpio_init_callback` +
      `gpio_add_callback`, deferring the actual read to a workqueue item out of interrupt
      context — reuse that shape for D0 rather than polling it
- [ ] Decide trigger vs. plain read: exposing D0 as a `SENSOR_TRIG_THRESHOLD` callback is
      the "proper" sensor-driver way, but given `device_status`/the UI only ever polls at
      ~1 Hz anyway (see ARCHITECTURE.md), a plain `gpio_pin_get_dt()` read inside
      `sample_fetch` alongside the ADC read is simpler and sufficient here — don't build
      the interrupt/trigger plumbing unless something actually needs sub-second latency
- [ ] Debounce: comparator outputs chatter right at the threshold edge; a few consecutive
      same-value samples (or `gpio-keys`-style debounce if going the interrupt route)
      before latching the digital status avoids flapping the UI/CAN status field

### 3.4 Cross-cutting

- [ ] Warm-up time: MQ-series need tens of seconds to several minutes of heater-on time
      before readings are trustworthy — track elapsed time since `init()` (`k_uptime_get()`),
      and until it's elapsed, either return `-EAGAIN` from `sample_fetch` or report
      `DEVICE_STATUS_WARN` through `channel_get`'s consumer rather than a misleadingly
      precise ppm number
- [ ] **MQ-7 heater cycling is a real datasheet requirement, not an optional nicety**: MQ-7
      alternates a 60s high-voltage (5V) heat phase with a 90s low-voltage (~1.4V) sense
      phase to get a usable CO reading, unlike MQ-2/MQ-3's simple constant-5V heater — decide
      explicitly whether v1 implements that cycle (needs PWM or a switched regulator on the
      heater pin, driven from a `k_timer`/delayable work item inside the driver) or
      documents the simplification of running MQ-7's heater at constant voltage with
      reduced accuracy. Don't silently do the latter without writing it down.
- [ ] Wire the analog+digital combined status into `device_status`: reuse the existing
      `device_status_set_environment(value, voltage, status)` seam (fold D0's alarm into
      the `status` argument — e.g. `DEVICE_STATUS_ERROR` when D0 trips) rather than adding
      a new field, unless the UI needs to show the raw digital bit independently of the
      ppm value, in which case follow the existing "one setter per producer" convention
      (see the `can_rx_count` note in §2) and add an explicit field
- [ ] Wire the driver into the build per the file list above; `env_sim.c`/`CONFIG_ENV_SIM`
      stays as the fallback until this replaces it as the real `device_status_set_environment`
      producer (that swap is a §4 sensor-sampling-thread concern, not part of the driver itself)
- [ ] Validate standalone with a simple polling sample (log ppm + Rs/Ro + D0 state to the
      console) before integrating into the main app — much easier to debug curve-fit and
      wiring mistakes without the UI/CAN/threading stack in the way

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

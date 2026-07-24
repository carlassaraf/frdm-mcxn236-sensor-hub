# FRDM-MCXN236 Sensor Hub

CAN-aware environmental/motion monitor with an LVGL touchscreen dashboard, built on the
NXP FRDM-MCXN236 (Zephyr RTOS). See [ROADMAP.md](ROADMAP.md) for the full project plan.

## Workspace setup

This project shares a single `west` workspace (and its `deps/` checkout) with other
projects under `zephyr-workspace/`. Only one manifest can be active at a time, so this
repo carries its own [west.yml](west.yml) with a `name-allowlist` trimmed to what this
board/project actually needs (`cmsis_6`, `hal_nxp`, `lvgl`) instead of the full HAL set
other projects in the workspace pull in.

To point the shared workspace at this project's manifest:

```sh
west config manifest.path frdm-mcxn236-sensor-hub
west update
```

Run both from anywhere inside the `zephyr-workspace` topdir. `west update` syncs `deps/`
against this project's `west.yml`.

**Switching back:** if you move to another project in the same workspace (e.g.
`zephyr-fundamentals`), point the manifest back at it and update again:

```sh
west config manifest.path zephyr-fundamentals
west update
```

Nothing gets deleted when you switch — modules that drop out of the active manifest's
allowlist are simply left on disk, unmanaged, until you switch back.

## Building

```sh
cd frdm-mcxn236-sensor-hub
west build -b frdm_mcxn236 app
west flash
```

## Repo layout

- `app/` — the sensor hub application (`prj.conf`, `boards/frdm_mcxn236.overlay`, `src/`)
- `west.yml` — this project's west manifest (see Workspace setup above)
- `ROADMAP.md` — project plan and progress notes

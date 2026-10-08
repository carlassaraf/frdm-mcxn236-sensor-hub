# Secure boot & firmware update: runbook

Step-by-step procedures for building, flashing, updating and *breaking* the MCUboot
setup on purpose (ROADMAP §7). For **why** it's built this way (the chain of trust, the
flash map, the sysbuild traps, why there's no revert), see
[ARCHITECTURE.md → Boot chain & image security](../ARCHITECTURE.md#boot-chain--image-security-mcuboot).

All commands run from the repo root (`frdm-mcxn236-sensor-hub/`) unless noted.
Verified on hardware 2026-10-08 with Zephyr v4.4.2 and MCUboot v2.4.0.

## 0. One-time setup

```sh
west update                                                     # pulls mcuboot + zcbor into deps/
pip install -r ../deps/bootloader/mcuboot/scripts/requirements.txt
pip install smpmgr                                              # SMP host tool
```

`imgtool` isn't installed as a command. Call the script directly:

```sh
IMGTOOL="python ../deps/bootloader/mcuboot/scripts/imgtool.py"
```

### Signing key

```sh
mkdir -p keys
$IMGTOOL keygen -k keys/sensor-hub-p256.pem -t ecdsa-p256
chmod 600 keys/sensor-hub-p256.pem
```

- `keys/` is gitignored. **Back the key up somewhere outside the repo.** If it's lost,
  the MCUboot on the board can't accept new images until MCUboot itself is reflashed
  over SWD with a new key.
- `app/sysbuild.conf` points at it with
  `${WEST_TOPDIR}/frdm-mcxn236-sensor-hub/keys/sensor-hub-p256.pem`. The path must
  resolve to an absolute path, because MCUboot resolves relative paths against its own
  source dir.

## 1. Build and flash (bootloader + app)

```sh
west build -p -b frdm_mcxn236/mcxn236 --sysbuild app
west flash                     # flashes MCUboot (0x0) then the signed app (slot0, 0x14000)
```

Always build with `--sysbuild`. A plain `west build` produces an app without MCUboot
that isn't signed and isn't linked for slot0.

**Check after any partition/config change:**

```sh
grep FLASH_LOAD_OFFSET build/*/zephyr/.config        # mcuboot: 0x0, app: 0x14000
grep -o "\-\-key [^ ]*" build/app/build.ninja        # must be keys/sensor-hub-p256.pem, not root-ec-p256.pem
ls -l build/app/zephyr/zephyr.signed.bin             # must stay below 483,328 B (slot0 size)
```

**Expected console** (J10, 115200 8N1):

```
*** Booting MCUboot v2.4.0 ***
I: Starting bootloader
I: Image index: 0, Swap type: none
I: Bootloader chainload address offset: 0x14000
I: Jumping to the first image slot
```

…followed by the application's own boot banner.

### Build outputs

| File | What it is |
|---|---|
| `build/mcuboot/zephyr/zephyr.bin` | Bootloader (contains the **public** key) |
| `build/app/zephyr/zephyr.bin` | App, **unsigned** (no MCUboot header) |
| `build/app/zephyr/zephyr.signed.bin` | App with header + hash + signature (what gets flashed/uploaded) |

### Flashing only one image

```sh
west flash --domain mcuboot                         # bootloader only
west flash --domain app                             # signed app only, into slot0
west flash -d build/app --bin-file <file.bin>       # any .bin into slot0 (0x10014000)
```

## 2. Firmware update over SMP (UART)

SMP shares the console UART (LPUART4 → MCU-Link VCOM). **Close the serial terminal
first.** Only one program can hold the port.

```sh
P="--port /dev/tty.usbmodemXXXX --baudrate 115200"    # ls /dev/tty.usbmodem*
```

1. Bump `app/VERSION` (e.g. `PATCHLEVEL`) so the new image can be told apart.
2. `west build` (**don't** flash).
3. Upload, mark, reset in one step:

   ```sh
   smpmgr $P upgrade build/app/zephyr/zephyr.signed.bin
   ```

   Or step by step:

   ```sh
   smpmgr $P image upload build/app/zephyr/zephyr.signed.bin
   smpmgr $P image state-read                 # slot1 shows the new version, pending=False
   smpmgr $P image state-write <slot1 hash>   # → pending=True
   smpmgr $P os reset
   ```

4. On the console MCUboot logs `Image 0 upgrade secondary slot -> primary slot` and
   the copy, then boots the new app. `image state-read` shows the new version in slot0.

Notes:
- **An uploaded image that isn't marked is never installed.** `pending=False` in slot1
  means MCUboot ignores it at reset.
- smpmgr's help talks about "test swap" and "revert". Neither exists here: in
  overwrite-only mode, marking means a **permanent** copy at the next reset (see
  ARCHITECTURE.md).
- The `Error reading MCUMgr parameters ... ENOTSUP` warning at connect is harmless. It
  goes away with `CONFIG_MCUMGR_GRP_OS_MCUMGR_PARAMS=y`.

## 3. Negative tests

Each test has to end with the **app not running** (or the old app still running) and the
listed log line. Recover as in §4.

### 3.1 Image signed with the dev key

The MCUboot dev key's private half ships publicly in the MCUboot repo, so anyone can
make this image. Sign the current build with it, using the same parameters as the real
build (copy them from `build/app/build.ninja` if the config changes):

```sh
$IMGTOOL sign --version 0.3.2 --header-size 0x400 --slot-size 483328 --overwrite-only --align 1 \
  --key ../deps/bootloader/mcuboot/root-ec-p256.pem \
  build/app/zephyr/zephyr.bin devkey.signed.bin

$IMGTOOL verify -k keys/sensor-hub-p256.pem devkey.signed.bin                        # fails
$IMGTOOL verify -k ../deps/bootloader/mcuboot/root-ec-p256.pem devkey.signed.bin     # passes
```

| Delivery | Result |
|---|---|
| Debugger into slot0: `west flash -d build/app --bin-file devkey.signed.bin` | MCUboot: `Image in the primary slot is not valid!` / `Unable to find bootable image`. Nothing runs |
| SMP: `smpmgr $P upgrade devkey.signed.bin` | The app **accepts** the upload (its header is well-formed). At reset, MCUboot rejects it, erases slot1, and **keeps booting the current app** (log below) |

Console after the SMP upload of the dev-key image (captured 2026-10-08):

```
*** Booting MCUboot v2.4.0 ***
*** Using Zephyr OS build v4.4.2 ***
I: Starting bootloader
I: Image index: 0, Swap type: test
E: Image in the secondary slot is not valid!
I: Bootloader chainload address offset: 0x14000
I: Image version: v0.3.1
I: Jumping to the first image slot
*** Booting Zephyr OS build v4.4.2 ***
```

How to read it:
- `Swap type: test` → slot1 was marked pending, so MCUboot tries to install it.
- `secondary slot is not valid!` → the signature check against the built-in public key
  failed.
- `Image version: v0.3.1` → the **old** app (not the 0.3.2 that was uploaded) is the
  one that boots.
- There's no "erasing" line. MCUboot scrambles an invalid secondary slot silently
  (`boot_scramble_slot()` in `bootutil/src/loader.c`). Afterwards
  `smpmgr $P image state-read` shows slot1 empty.

The SMP case is the "a bad update can't brick it" property. The app only checks the
header *format*. The cryptographic check happens in MCUboot.

### 3.2 Unsigned image

| Delivery | Result |
|---|---|
| Debugger: `west flash -d build/app --bin-file build/app/zephyr/zephyr.bin` | MCUboot: `Image in the primary slot is not valid!` (no header magic) |
| SMP upload of `zephyr.bin` | Rejected **by the app** at upload (no MCUboot header). Nothing is written |

### 3.3 Corrupted image (hash mismatch)

```sh
cp build/app/zephyr/zephyr.signed.bin corrupt.bin
printf '\x00' | dd of=corrupt.bin bs=1 seek=8192 conv=notrunc
```

Flash or upload it as above. It's rejected exactly like 3.1: same log lines, but this
time the stored hash fails instead of the signature.

### 3.4 App built for a different MCUboot

Flash only the app (`west flash --domain app`) built with key A onto a board whose
MCUboot was built with key B → `Image in the primary slot is not valid!`. **Changing
the key always means reflashing MCUboot** (`west flash`).

Delete the test images afterwards (`rm devkey.signed.bin corrupt.bin`). They're
gitignored, but they don't belong in the repo dir.

## 4. Recovery

| Situation | Fix |
|---|---|
| A rejected app in slot0 | `west flash --domain app` (a correctly signed build) |
| A rejected update in slot1 | Nothing to do. MCUboot erased it and kept the old app |
| A broken or erased MCUboot, or a key change | `west flash` (both images) |
| Anything weird | Mass erase with LinkServer/pyocd, then `west flash` |

SWD is never locked on this board, so every state can be recovered with the debugger.
**Don't** touch CMPA/CFPA, debug-lock or lifecycle settings. That's the one area on
this SoC where mistakes are permanent.

## 5. Troubleshooting

| Symptom | Cause |
|---|---|
| `Image in the primary slot is not valid!` right after a partition change | MCUboot and the app disagree on the flash map. Both must include `app/dts/partitions.dtsi` (ARCHITECTURE.md, trap 1) |
| `west flash` → `KeyError: 'CONFIG_FLASH_LOAD_OFFSET'` | A partition node got `compatible = "zephyr,mapped-partition"` (a newer Zephyr scheme). Remove it in v4.4.2 |
| MCUboot doesn't start or the app doesn't link at 0x14000 | `app/sysbuild/mcuboot.overlay` lost the `zephyr,code-partition = &boot_partition` chosen node (trap 2) |
| `MCUBoot bootloader key file` path not found | A relative key path. Use `${WEST_TOPDIR}/…` or an absolute path |
| MCUmgr Kconfig warnings, SMP silently missing | `zcbor` missing from the `west.yml` allowlist, or `west update` not run |
| smpmgr times out | The serial terminal still holds the port, wrong `/dev/tty.usbmodem*`, or heavy logging during upload |
| Upload OK, nothing changes after reset | The image wasn't marked (`pending=False`) |

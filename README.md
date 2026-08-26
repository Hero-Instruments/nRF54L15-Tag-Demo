# nRF54L15 Tag Demo

Firmware for the Nordic **nRF54L15 Tag** demonstrating Bluetooth **Channel Sounding**
(distance ranging) together with the tag's onboard sensors and BLE telemetry
services. Built on the nRF Connect SDK **v3.4.0**.

## Features

- **Bluetooth Channel Sounding** — dual-role (initiator *and* reflector) and
  dual-mode (**RAS** GATT ranging + inline-**PCT/IPT**), with `cs_de`
  distance estimation, a static calibration offset, and
  **accelerometer-assisted stabilization**.
- **Onboard sensors** — BME688 environmental, ADXL367 accelerometer, BMI270 IMU;
  periodic sampling + logging with runtime ODR/range control.
- **Standard BLE services** — Environmental Sensing Service (ESS, `0x181A`):
  temperature / humidity / pressure (read + notify); Device Information Service
  (DIS, `0x180A`): manufacturer, model, firmware revision, per-unit serial.
- **Custom BLE service** — Motion Service: high-rate accelerometer & gyroscope
  streaming over notifications, plus a runtime config characteristic
  (see [docs/MOTION_SERVICE.md](docs/MOTION_SERVICE.md)).
- **High-rate IMU sampling** — configurable poll rate up to the sensor ODR,
  buffered for BLE streaming.
- **RTT shell** — runtime control of roles, ranging, sensors, and logging.
- **Remote shell over BLE** — the same shell commands are reachable via
  MCUmgr/SMP over Bluetooth (nRF Connect Device Manager or the `mcumgr` CLI).
- **OTA firmware update** — MCUboot bootloader with dual image slots; a signed
  image is uploaded over the same SMP-over-BLE link, and the tag self-confirms
  the new image or automatically reverts if it fails to run.
- **Initiator doubles as a peripheral** — while ranging (as a BLE *central* to the
  reflector) the initiator also advertises and accepts a second link from a
  screen/phone/SMP host (`BT_MAX_CONN=2`). A `cs distance` shell command lets that
  host poll the live distance (e.g. to show it on a screen).
- **Shorter Connection Intervals (SCI)** — the CS initiator requests a sub-7.5 ms
  connection interval (Core 6.x SCI feature) for faster ranging cadence on the
  tag↔tag link.
- **Two-tag ranging demo** — a button cycles the CS role and the RGB LED shows
  the role and the live distance (color + blink rate); no PC required. A
  **short-distance demo** mode (`APP_UX_SHORT_DEMO`) remaps the LED to a reactive
  1–3 ft range. Distance is reported in **feet + inches**.

## Hardware / target

- **Board target:** `nrf54l15tag/nrf54l15/cpuapp` (NCS v3.4.0, sysbuild).
- **No UART** — console, logging, and the shell all run over **SEGGER RTT**
  (the app is built with the `rtt-console` snippet).
- **Onboard sensors** (aliases used by the firmware):

  | Alias | Part | Driver | Bus | Notes |
  |-------|------|--------|-----|-------|
  | `env0` | BME688 | `bosch,bme680` | I²C (`i2c21` @ 0x76) | temp / humidity / pressure / gas |
  | `accel0` | ADXL367 | `adi,adxl367` | I²C (`i2c21` @ 0x1d) | low-power accelerometer |
  | `imu0` | BMI270 | `bosch,bmi270` | SPI (`spi22`) | accel + gyro, IRQ P1.04 |

- **RGB LED** (`led1_red/green/blue`) and **button** (`sw0`) drive the two-tag demo UX.
- **Channel Sounding antenna switch** — the app overlay
  ([boards/nrf54l15tag_nrf54l15_cpuapp.overlay](boards/nrf54l15tag_nrf54l15_cpuapp.overlay))
  configures the on-board SKY13348 as a 2-antenna CS switch (4 antenna paths).

## Repository structure

`main.c` initializes each module; every functional area is a self-contained
module under `src/` with its own `CMakeLists.txt` and `Kconfig`, toggled by a
`CONFIG_APP_*` symbol.

| Path | Symbol | Responsibility |
|------|--------|----------------|
| `src/sensors/` | `APP_SENSORS` | Sensor sampling/logging, runtime ODR/range, high-rate IMU stream queue, stationarity signal |
| `src/ble/` | `APP_BLE_CORE` / `APP_ADV` | BLE stack enable, connection & security callbacks, role-aware **dual-connection** tracking (central CS link + peripheral host link), and the role-independent connectable **advertiser** (`adv.c`) |
| `src/cs/` | `APP_CS` | Channel Sounding: shared state machine, initiator, reflector, distance filtering |
| `src/ess/` | `APP_ESS` | Environmental Sensing Service (GATT server) |
| `src/dis/` | `APP_DIS` | Device Information Service serial number (the service itself is Zephyr's `CONFIG_BT_DIS`) |
| `src/motion/` | `APP_MOTION` | Custom Motion Service (accel/gyro notify + config char) |
| `src/ux/` | `APP_UX` | Button role-cycling + RGB-LED role/distance indication |
| `src/control/` | `APP_CONTROL` | RTT shell commands |
| `src/dfu/` | `APP_DFU` | OTA image confirm/revert policy + quiesces CS during an upload |

Top-level: `CMakeLists.txt`, `Kconfig`, `prj.conf`, `Kconfig.sysbuild`,
`sysbuild.conf` (enables MCUboot), `VERSION` (stamps the signed image),
`boards/nrf54l15tag_nrf54l15_cpuapp.{overlay,conf}`, `sysbuild/` (MCUboot and
IPC-radio image configs), and `docs/MOTION_SERVICE.md` (client-facing Motion
protocol spec).

## Building

Built with the **nRF Connect for VS Code** extension (NCS v3.4.0): board target
`nrf54l15tag/nrf54l15/cpuapp` with the **`rtt-console`** snippet enabled (all
console/log/shell I/O is over SEGGER RTT — open an RTT terminal to interact).
Build output directories (`build*/`) are gitignored.

This is a **sysbuild** build producing two images — MCUboot and the signed
application (see [Firmware update over BLE (OTA)](#firmware-update-over-ble-ota)).
`west flash` programs both; `merged_nrf54l15tag_nrf54l15_cpuapp.hex` in the build
directory is the equivalent single-file image.

> **A build directory created before MCUboot was added must be rebuilt pristine.**
> Sysbuild cannot add a second image to an existing build tree incrementally, so
> `rm -rf build` (or `--pristine`) is required once. Until you do, that directory
> still holds a single, pre-MCUboot application — and flashing it reports success
> while leaving the tag running old firmware or nothing at all (no LED, no button
> response, silent RTT). Check `build/domains.yaml`: it must list **both**
> `mcuboot` and `nRF54L15TagDemo`.
>
> For the same reason, never flash `zephyr.signed.bin`, the standalone
> `mcuboot.hex`, or the `.elf` on their own over J-Link — the application links at
> `0x10000` and needs MCUboot at `0x0` beneath it. Use `west flash`, or the
> `factory.hex` release asset.

### Workspace setup (west)

This repo is also a **west manifest repository** ([west.yml](west.yml), NCS
v3.4.0 pinned), so it can be bootstrapped into a self-contained, reproducible
workspace:

```sh
west init -m https://github.com/onceLabs/nRF54L15-Tag-Demo --mr main my-workspace
cd my-workspace
west update --narrow -o=--depth=1     # shallow-clone the allow-listed SDK modules
west build -b nrf54l15tag/nrf54l15/cpuapp -S rtt-console nRF54L15TagDemo
west flash
```

The allow-list includes `mcuboot` — the bootloader is part of the build, so
removing it would break OTA support.

The manifest uses an `import` allow-list to pull only the SDK modules this
application needs (~2.8 GB working tree vs the full ~4.6 GB NCS); `--narrow
-o=--depth=1` shallow-clones to reduce the download further. Using the nRF
Connect VS Code extension against an already-installed NCS works unchanged and
does not require this step.

## Runtime control (RTT shell)

Open the RTT console/terminal after flashing. Commands:

**Channel Sounding — `cs`**

| Command | Description |
|---------|-------------|
| `cs role <initiator\|reflector>` | Select role (applied on next start) |
| `cs mode <ras\|ipt>` | Select ranging transport |
| `cs start` / `cs stop` | Start / stop ranging |
| `cs status` | Show role / mode / running + latest distance |
| `cs distance` | Latest distance: `distance mm=<n> ft=<n> in=<f>` (or `distance: none`) |

**Sensors — `sensor`**

| Command | Description |
|---------|-------------|
| `sensor log <env\|accel\|imu\|all> <on\|off>` | Gate per-peripheral data logging |
| `sensor odr <accel\|imu-accel\|imu-gyro> <hz>` | Set output data rate |
| `sensor range <accel\|imu-accel\|imu-gyro> <val>` | Set full-scale (accel G / gyro dps) |
| `sensor stream start\|stop` / `sensor stream rate <hz>` | High-rate IMU stream control |
| `sensor status` | Show log/ODR/range/stream + motion (stationary) state |

**Logging** — Zephyr's built-in runtime filtering is enabled:
`log enable <level> <module>` / `log disable <module>`
(modules: `app_cs`, `app_sensors`, `app_ble_core`, `app_motion`, …).

**Remotely over BLE (SMP)** — the same commands run through MCUmgr's shell
management group, so no RTT cable is needed. With the `mcumgr` CLI:
```
mcumgr --conntype ble --connstring peer_name='nRF54L15 Tag' shell exec "cs status"
mcumgr --conntype ble --connstring peer_name='nRF54L15 Tag' shell exec "cs distance"
mcumgr --conntype ble --connstring peer_name='nRF54L15 Tag' shell exec "sensor status"
```
or use the nRF Connect **Device Manager** app (connect → Shell). The command
output returned over SMP matches the RTT console, and the local RTT shell keeps
working at the same time. The SMP service is **open** (no pairing required — see
[Notes & limitations](#notes--limitations)), so a screen/host can connect and poll
`cs distance` while the tag ranges.

## Functionality & how to demo

### Onboard sensors
On boot, all three sensors sample at 1 Hz and log over RTT. Tilt the board
(accel/IMU) or breathe on it (BME688 humidity/temp) to see values move. Use
`sensor log …` to silence a noisy peripheral, and `sensor odr`/`sensor range`
to reconfigure the IMU at runtime.

> The tag advertises (and is connectable for ESS / Motion / SMP) whenever a
> peripheral connection slot is free — when **idle, a reflector, *or* an
> initiator**. With `BT_MAX_CONN=2` the initiator keeps its outbound (central) link
> to the reflector *and* accepts an inbound (peripheral) link from a host, so a
> phone/screen can read ESS/Motion/SMP and poll `cs distance` while ranging
> continues. Advertising stops only once that host link is connected.

### Device Information Service (DIS)
Connect and read the standard DIS characteristics (`0x180A`) — no app-specific
knowledge needed, any generic BLE client shows them:

| Characteristic | UUID | Value |
|---|---|---|
| Manufacturer Name | `0x2A29` | `Hero Instruments, Inc` |
| Model Number | `0x2A24` | `nRF54L15 Tag` |
| Firmware Revision | `0x2A26` | e.g. `0.0.7+0` — derived from [`VERSION`](VERSION) |
| Serial Number | `0x2A25` | the tag's Bluetooth address, e.g. `E1A2B3C4D5F6` |

**Firmware Revision is the quickest way to confirm which image a tag is actually
running** after an OTA — no RTT cable, no SMP image-list query. The string is not
hard-coded: `CONFIG_BT_DIS_FW_REV_STR` is left at its Kconfig default,
`$(APP_VERSION_TWEAK_STRING)`, which is the same value `imgtool` stamps into the
MCUboot image header, so DIS and the OTA metadata cannot disagree.

The Serial Number is written at boot by [`src/dis/`](src/dis/) from the Bluetooth
identity address, so it matches the address a scanner shows and is unique per
unit. PnP ID is deliberately disabled — Zephyr enables it by default with Vendor
ID `0x0000`, which is not an assigned ID.

### Environmental Sensing Service (ESS)
With a BLE central (e.g. the **nRF Connect** phone app), scan for **`nRF54L15 Tag`**
(it advertises the ESS UUID `0x181A`), connect, and read or subscribe to the
Temperature (`0x2A6E`), Humidity (`0x2A6F`), and Pressure (`0x2A6D`)
characteristics. Values update ~1 Hz.

### Motion Service (custom, high-rate accel + gyro)
Connect and discover the custom Motion Service (128-bit UUID). Enable
notifications on the **Accel** and/or **Gyro** characteristic to auto-start
high-rate streaming; write the **Config** characteristic to set poll rate, ODR,
and range from the client. Full byte-level protocol + Python/TypeScript parsing
examples are in **[docs/MOTION_SERVICE.md](docs/MOTION_SERVICE.md)**.

### High-rate IMU streaming (RTT)
`sensor odr imu-accel 1600` then `sensor stream start` — the RTT log reports the
effective sample rate (`imu stream: ~N Hz`) and dropped-sample count. `sensor
stream rate <hz>` sets the poll rate independently of the ODR. The same stream
is what the Motion Service drains over BLE.

### Channel Sounding (shell-driven)
Two BLE devices are required — the tag can be either end, and can pair with a
second tag or the NCS `channel_sounding_*` DK samples (both ends must use the
same **mode**).
1. On device A: `cs role reflector` → `cs start` (advertises).
2. On device B: `cs role initiator` → `cs start` (scans, connects, ranges).
3. The initiator prints `distance[ap0]: <ft/in>` over RTT (feet + inches, total
   inches in parentheses). Switch `cs mode ras|ipt` on both ends to compare
   transports.

### Two-tag button + LED demo (no PC)
With the UX module (default), a tag boots idle (LED off). Press the button to
cycle **Off → Reflector → Initiator**:
- **Reflector:** solid **blue**.
- **Initiator, searching:** solid **white**; once ranging, the LED shows distance
  as a **color zone** — green `< near`, yellow `near–far`, red `≥ far` — **blinking**
  faster as it gets closer (blink period interpolates from `APP_UX_BLINK_FAST_MS`
  ≈ 120 ms at contact to `APP_UX_BLINK_SLOW_MS` ≈ 1200 ms at the far threshold,
  i.e. ~8 Hz up close → ~0.8 Hz far).

Default zones are `< 1 m` / `1–3 m` / `≥ 3 m`. Set **`APP_UX_SHORT_DEMO=y`** to
remap them to a close-range **1 ft / 3 ft** span (green `< 1 ft`, yellow `1–3 ft`,
red `> 3 ft`); because the blink is far-relative it becomes markedly more reactive
across that short range.

Set one tag to Reflector and the other to Initiator; the initiator connects and
starts ranging automatically.

### Distance calibration & accelerometer stabilization
- A static offset (`APP_CS_DISTANCE_OFFSET_MM`, default −1610 mm) is added to every
  estimate to cancel the fixed RF/antenna-path bias (measured from two tags at a
  known short distance; retune for your hardware). The corrected value is clamped ≥ 0.
- When `APP_CS_STABILIZE` is set, the initiator's accelerometer is used as a
  motion signal: when the tag is **stationary** the distance filter widens its
  median window and rejects implausible jumps (steady reading); when **moving**
  it shortens the window to track quickly. `sensor status` shows the current
  stationary/moving state.

## Firmware update over BLE (OTA)

The build includes **MCUboot** as a first-stage bootloader (enabled in
[sysbuild.conf](sysbuild.conf)), so the application can be replaced over the same
SMP-over-Bluetooth link used for the remote shell — no J-Link needed after the
first flash.

### Memory layout

Partitions come from devicetree (the SoC's `nrf54l15_cpuapp_partition.dtsi`; no
Partition Manager). MCUboot runs in **swap-using-move** mode: the new image is
uploaded into slot1 and swapped into slot0 on the next reset.

| Partition | Address | Size | Contents |
|-----------|---------|------|----------|
| `boot_partition` | `0x0` | 62 K | MCUboot (currently ~51 K used) |
| `slot0_partition` | `0x10000` | 712 K | running application (currently ~443 K used) |
| `slot1_partition` | `0xc2000` | 712 K | upload target for the new image |
| `storage_partition` | `0x174000` | 36 K | unused by this app |

### Artifacts

| File (under `build/`) | Use |
|-----------------------|-----|
| `merged_nrf54l15tag_nrf54l15_cpuapp.hex` | first-time / factory flash over J-Link (MCUboot + app) |
| `nRF54L15TagDemo/zephyr/zephyr.signed.bin` | **the OTA payload** — upload this |
| `dfu_application.zip` | same image packaged for nRF Connect Device Manager / nRF Util |

Tagged builds publish these same files as **GitHub Release** assets, so you don't
have to build locally to get them:

| Release asset | Build output it comes from |
|---------------|----------------------------|
| `nrf54l15tag-<tag>-factory.hex` | `merged_nrf54l15tag_nrf54l15_cpuapp.hex` |
| `nrf54l15tag-<tag>-ota.bin` | `zephyr.signed.bin` |
| `nrf54l15tag-<tag>-dfu.zip` | `dfu_application.zip` |
| `nrf54l15tag-<tag>-mcuboot.hex` | `mcuboot/zephyr/zephyr.hex` (bootloader alone) |
| `nrf54l15tag-<tag>.elf` | `zephyr.elf` — symbols, for decoding a crash from that release |
| `SHA256SUMS.txt` | checksums of the above |

### Releases (CI)

[`.github/workflows/release.yml`](.github/workflows/release.yml) builds and publishes a
release on every `v*` tag; [`build.yml`](.github/workflows/build.yml) builds every PR and
push to `main` so breakage shows up before you tag. Both build inside Nordic's
`ghcr.io/nrfconnect/sdk-nrf-toolchain:v3.4.0` container and set up the workspace from
[west.yml](west.yml), sharing the steps in
[`.github/actions/ncs-build`](.github/actions/ncs-build/action.yml).

To cut a release, run [`scripts/release.sh`](scripts/release.sh). It bumps
[VERSION](VERSION), commits, tags and pushes — showing you exactly what it will do and
asking before anything leaves the machine:

```sh
scripts/release.sh              # patch: 1.0.0 -> 1.0.1
scripts/release.sh minor        # 1.0.0 -> 1.1.0
scripts/release.sh major        # 1.0.0 -> 2.0.0
scripts/release.sh 1.2.0        # an explicit version
scripts/release.sh 1.0.1-rc1    # VERSION 1.0.1, tag v1.0.1-rc1 (pre-release)

scripts/release.sh --dry-run    # show the plan and exit
scripts/release.sh --no-push    # bump, commit and tag locally only
```

It refuses to run on a dirty tree, off `main`, when `main` disagrees with `origin/main`,
or when the tag already exists — all before touching anything, so a rejected release
leaves the tree exactly as it was. The final push is `--atomic`, so the tag can never
land without its commit.

The equivalent by hand:

```sh
# 1. bump VERSION (this stamps the MCUboot image header)
$EDITOR VERSION
git commit -am "release 1.0.1"
# 2. tag and push
git tag v1.0.1
git push --atomic origin main v1.0.1
```

Either way the tag must agree with [VERSION](VERSION) — `v1.0.1` requires
`VERSION_MAJOR/MINOR/PATCHLEVEL = 1/0/1`. The workflow checks this and **fails on
mismatch** rather than publishing a release whose image header contradicts its name. A
suffixed tag (`v1.0.1-rc1`) is allowed against the same `VERSION` and is marked as a
**pre-release**.

> Release assets are signed with MCUboot's public development key, so a downloaded
> asset is *not* a trusted image — see [Security](#security) below.

Bump [VERSION](VERSION) before building an update; the version is written into
the image header, shown by `mcuboot` / `mcumgr image list`, and logged at boot.

### Updating a tag

From the nRF Connect **Device Manager** app (Android/iOS): connect to
`nRF54L15 Tag` → **Images** → select `zephyr.signed.bin` (or the `.zip`) →
**Test**. The tag resets, MCUboot swaps the image in, and the new firmware boots.

Or with the `mcumgr` CLI:

```sh
mcumgr --conntype ble --connstring peer_name='nRF54L15 Tag' image upload zephyr.signed.bin
mcumgr --conntype ble --connstring peer_name='nRF54L15 Tag' image list
mcumgr --conntype ble --connstring peer_name='nRF54L15 Tag' image test <hash-of-slot1>
mcumgr --conntype ble --connstring peer_name='nRF54L15 Tag' reset
```

Slot state is also visible from the shell (RTT, or over BLE via
`shell exec "mcuboot"`).

### Test / confirm / revert

MCUboot boots a freshly uploaded image as a **test** image. The `src/dfu/` module
confirms it automatically after `APP_DFU_CONFIRM_DELAY_S` (default **10 s**) of
uptime — so you do not have to press *Confirm* — while still giving rollback
protection: anything that resets the tag before that deadline (fault, watchdog,
power loss) leaves the image unconfirmed and MCUboot reverts to the previous one
on the next boot. Confirming from a host works too, and cancels the timer.

While an upload is in progress the module stops Channel Sounding
(`APP_DFU_PAUSE_CS`), because writing the image into RRAM stalls the CPU in
bursts and disturbs CS subevent timing. Pick the role again with the button (or
`cs start`) after the update.

### Security

The SMP service is **unauthenticated** (see *Notes & limitations*), so
**MCUboot's signature check is the only thing gating what runs on the tag** — and
this build signs with MCUboot's stock development key
(`bootloader/mcuboot/root-ed25519.pem`, ED25519 + SHA512), which is public. That
is fine for a demo but means anyone can build an image this tag will accept. For
production, point `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` at a private project key,
or provision a key into the nRF54L15's **KMU**
(`SB_CONFIG_MCUBOOT_SIGNATURE_USING_KMU`).

## Configuration reference

Each module is enabled by its `CONFIG_APP_*` symbol; key tunables:

| Kconfig | Default | Purpose |
|---------|---------|---------|
| `APP_CS_DEFAULT_ROLE_*` / `APP_CS_DEFAULT_MODE_*` | reflector / IPT | Boot role & ranging mode |
| `APP_CS_AUTOSTART` | off when `APP_UX` | Auto-start ranging at boot |
| `APP_CS_DISTANCE_OFFSET_MM` | −1610 | Static distance calibration offset (clamped ≥ 0) |
| `APP_CS_STABILIZE` / `APP_CS_STAB_GATE_MM` / `APP_CS_STAB_WINDOW_MOVING` | y / 1000 / 3 | Accel-assisted filtering |
| `APP_SENSORS_STATIONARY_RATE_HZ` / `_WINDOW` / `_THRESH_MMS2` | 25 / 25 / 300 | Stationarity detector |
| `APP_MOTION_CONN_INTERVAL_MIN/MAX` | 6 (7.5 ms) | Requested motion-stream connection interval |
| `APP_CS_SHORT_INTERVAL` / `APP_CS_SHORT_INTERVAL_US` | y / 3750 | Initiator requests a sub-7.5 ms SCI interval (needs `BT_SHORTER_CONNECTION_INTERVALS`) |
| `APP_UX_SHORT_DEMO` | n | Remap LED zones to a reactive 1 ft / 3 ft range |
| `APP_UX_NEAR_MM` / `APP_UX_FAR_MM` | 1000 / 3000 (305 / 914 if short-demo) | LED distance color thresholds (green/yellow/red) |
| `APP_UX_BLINK_FAST_MS` / `APP_UX_BLINK_SLOW_MS` | 120 / 1200 | Blink period at contact / at the far threshold (interpolated) |
| `BT_MAX_CONN` | 2 | Central CS link + peripheral host link simultaneously |
| `MCUMGR` / `MCUMGR_TRANSPORT_BT` / `MCUMGR_GRP_SHELL` / `SHELL_BACKEND_DUMMY` | y | SMP-over-BLE remote shell (MCUmgr; also needs `ZCBOR`) |
| `MCUMGR_TRANSPORT_BT_PERM_RW` | y | SMP service open — no encryption/pairing (see limitations) |
| `SB_CONFIG_BOOTLOADER_MCUBOOT` (`sysbuild.conf`) | y | Build MCUboot so the app can be updated over the air |
| `MCUMGR_GRP_IMG` / `IMG_MANAGER` / `MCUBOOT_SHELL` | y | OTA image upload + `mcuboot` slot commands |
| `APP_DFU_CONFIRM_DELAY_S` | 10 | Healthy uptime before a test image self-confirms (0 = immediately) |
| `APP_DFU_PAUSE_CS` | y | Stop ranging while an image upload is in flight |
| antenna paths (`boards/…cpuapp.conf`) | 2 antennas / 4 paths | CS antenna switching |

## Notes & limitations

- The NCS v3.4.0 BMI270 driver has **no FIFO/RTIO**, so high-rate sampling is
  paced polling. 1000 Hz is not a native BMI270 ODR (steps are …/400/800/**1600**),
  so a 1000 Hz request quantizes to 800 Hz.
- Accelerometer stabilization senses the **initiator's** motion only (reflector
  movement isn't captured).
- Requested connection intervals below 7.5 ms need controller/central support;
  the peripheral can only request ≥ 7.5 ms.
- Both Channel Sounding endpoints must use the **same role pairing and mode**
  (RAS ↔ RAS or IPT ↔ IPT); the two transports don't interoperate.
- **Security:** the tag is `NoInputNoOutput` (no pairing/IO-capability callbacks), so
  authenticated (MITM) pairing isn't possible. The SMP service is therefore left **open**
  (`MCUMGR_TRANSPORT_BT_PERM_RW`) — any host can run shell commands over BLE without
  pairing; acceptable for a demo, not for production. The CS tag↔tag link is still
  encrypted (Just-Works L2, required by CS and the RAS GATT service).
- **Known issue:** connecting a host to the tag *while it is an initiator ranging* can
  currently disrupt the CS session (the host link's connect/security events perturb the
  initiator state machine). Pending a connection-callback role-gating fix; until then,
  prefer connecting the host to a reflector/idle tag, or expect ranging to hiccup.

## License

This project is licensed under the **BSD 3-Clause License** —
Copyright (c) 2026 onceLabs. See [LICENSE](LICENSE) for the full text; each source
file carries an `SPDX-License-Identifier: BSD-3-Clause` header.

Portions derive from the nRF Connect SDK and Zephyr samples, which are licensed
under the Nordic 5-Clause BSD and Apache-2.0 licenses by their respective copyright
holders.

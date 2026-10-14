# Developer Setup Guide

This guide is for a developer who is comfortable with embedded C but has **not used Zephyr or
the nRF Connect SDK (NCS) before**. It takes you from a bare machine to:

1. a working NCS **v3.4.0** install,
2. a local build of this firmware,
3. that build flashed onto an nRF54L15 Tag,
4. a live shell on the tag over RTT, and
5. your first change built and running.

Expect roughly an hour for the walkthrough, plus download time. If you only want to *demo* a
tag that's already flashed, read [DEMO_GUIDE.md](DEMO_GUIDE.md) instead.

---

## Contents

1. [Learn the basics first (Nordic tutorials)](#1-learn-the-basics-first-nordic-tutorials)
2. [Concepts in five minutes](#2-concepts-in-five-minutes)
3. [What you need](#3-what-you-need)
4. [Install the SDK and tools](#4-install-the-sdk-and-tools)
5. [Get the code](#5-get-the-code)
6. [Build](#6-build)
7. [Connect the hardware and flash](#7-connect-the-hardware-and-flash)
8. [Talk to the tag over RTT](#8-talk-to-the-tag-over-rtt)
9. [Check that it works](#9-check-that-it-works)
10. [Updating over Bluetooth (OTA)](#10-updating-over-bluetooth-ota)
11. [Your first change](#11-your-first-change)
12. [Troubleshooting](#12-troubleshooting)
13. [Where to go next](#13-where-to-go-next)

---

## 1. Learn the basics first (Nordic tutorials)

Nordic's free **DevAcademy** courses are the best introduction to this toolchain. You don't
need to finish them before you start, but the first two lessons of *Fundamentals* will make
the rest of this guide much easier to follow.

| Order | Resource | Why it matters here |
|-------|----------|---------------------|
| 1 | [nRF Connect SDK Fundamentals](https://academy.nordicsemi.com/courses/nrf-connect-sdk-fundamentals/) (DevAcademy) | **Start here.** Covers installing NCS, the VS Code extension, building and flashing, and the three ideas this project uses everywhere: Kconfig, devicetree and logging. Lessons 1–4 are the most relevant; lessons 7–8 (threads, synchronisation) help when you read `src/sensors/` and `src/motion/`. |
| 2 | [Bluetooth Low Energy Fundamentals](https://academy.nordicsemi.com/courses/bluetooth-low-energy-fundamentals/) (DevAcademy) | Advertising, connections, GATT and security. Everything in `src/ble/`, `src/ess/`, `src/motion/` and `src/tput/` builds on this. |
| 3 | [nRF Connect SDK Intermediate](https://academy.nordicsemi.com/courses/nrf-connect-sdk-intermediate/) (DevAcademy) | Lesson 2 (debugging), lesson 8 (**sysbuild**) and lesson 9 (**bootloaders and DFU/FOTA**) explain how this project builds two images and updates over the air. |
| 4 | [Bluetooth Channel Sounding: from theory to practice](https://www.nordicsemi.com/Resources/Training/Webinars/2025/Bluetooth-Channel-Sounding-From-theory-to-practice-on-the-nRF54L-Series-and-Android) (Nordic webinar) | Background on Channel Sounding (CS), the distance-measurement feature at the heart of this demo. |

Reference documentation you'll come back to:

- **NCS v3.4.0 documentation:** [Installing the nRF Connect SDK](https://nrfconnectdocs.nordicsemi.com/ncs/3.4.0/nrf/installation/install_ncs.html)
- **Channel Sounding samples this project is based on:**
  [RAS initiator](https://nrfconnectdocs.nordicsemi.com/ncs/3.4.0/nrf/samples/bluetooth/channel_sounding/ras_initiator/README.html),
  [RAS reflector](https://nrfconnectdocs.nordicsemi.com/ncs/3.4.0/nrf/samples/bluetooth/channel_sounding/ras_reflector/README.html),
  [IPT initiator](https://nrfconnectdocs.nordicsemi.com/ncs/3.4.0/nrf/samples/bluetooth/channel_sounding/ipt_initiator/README.html),
  [IPT reflector](https://nrfconnectdocs.nordicsemi.com/ncs/3.4.0/nrf/samples/bluetooth/channel_sounding/ipt_reflector/README.html),
  and the [distance estimation library (`cs_de`)](https://nrfconnectdocs.nordicsemi.com/ncs/3.4.0/nrf/libraries/bluetooth/cs_de.html)
- **nRF Connect for VS Code:** [extension documentation](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/index.html)
- **The board:** [nRF54L15 TAG board page](https://docs.zephyrproject.org/latest/boards/nordic/nrf54l15tag/doc/index.html) (Zephyr docs)
- **Zephyr build system:**
  [west](https://docs.zephyrproject.org/latest/develop/west/index.html),
  [Kconfig](https://docs.zephyrproject.org/latest/build/kconfig/index.html),
  [devicetree](https://docs.zephyrproject.org/latest/build/dts/index.html),
  [snippets](https://docs.zephyrproject.org/latest/build/snippets/index.html)
  (and the [`rtt-console` snippet](https://docs.zephyrproject.org/latest/snippets/rtt-console/README.html)),
  [sysbuild](https://docs.zephyrproject.org/latest/build/sysbuild/index.html)
- **Questions:** [Nordic DevZone](https://devzone.nordicsemi.com/) is Nordic's support forum,
  and Nordic engineers answer there.

> The Zephyr links above point at Zephyr's *latest* docs. NCS v3.4.0 ships its own Zephyr
> fork, so on the rare occasion a detail disagrees, the NCS v3.4.0 docs win.

---

## 2. Concepts in five minutes

Zephyr projects have more moving parts than a typical vendor-HAL project. Here is each one,
and where it shows up in this repository.

| Concept | What it is | In this repo |
|---------|-----------|--------------|
| **Zephyr** | An open-source RTOS: kernel, drivers, Bluetooth stack, shell and logging. | Everything under `zephyr/` in the SDK. |
| **nRF Connect SDK (NCS)** | Nordic's distribution of Zephyr, with Nordic's Bluetooth controller, libraries (such as `cs_de` for Channel Sounding distance estimation) and samples. It's pinned to a version; **this project needs exactly v3.4.0**. | Installed at e.g. `/opt/nordic/ncs/v3.4.0` (macOS) or `C:\ncs\v3.4.0` (Windows). |
| **Toolchain** | The compiler, CMake, Ninja, Python, `west` and so on, bundled and versioned to match an SDK. | Installed alongside the SDK by the VS Code extension or `nrfutil`. |
| **west** | Zephyr's command-line tool. It fetches SDK repositories (`west update`) and wraps the build (`west build`) and flash (`west flash`) steps. | [west.yml](../west.yml) is a *manifest* that lists exactly which SDK repositories this app needs. |
| **Board target** | A string that identifies board, SoC and CPU. | `nrf54l15tag/nrf54l15/cpuapp`: the Tag board, nRF54L15 SoC, application core. |
| **Kconfig** | Compile-time feature switches (`CONFIG_FOO=y`), similar to the Linux kernel's. Every driver and subsystem is enabled this way. | [prj.conf](../prj.conf) (app-wide), [boards/…cpuapp.conf](../boards/nrf54l15tag_nrf54l15_cpuapp.conf) (board-specific), and one `Kconfig` file per module under `src/` that defines this app's own `CONFIG_APP_*` options. |
| **Devicetree (DTS)** | A description of the hardware: pins, buses, sensors. Drivers are bound to hardware through it, not through `#define`s. | [boards/…cpuapp.overlay](../boards/nrf54l15tag_nrf54l15_cpuapp.overlay) adds the sensor aliases (`env0`, `accel0`, `imu0`) and the Channel Sounding antenna switch on top of the board's own DTS. |
| **Snippet** | A named, reusable bundle of Kconfig and DTS applied with `-S <name>`. | **`rtt-console` is mandatory.** The Tag has no UART, so without this snippet you get no console, logs or shell. |
| **Sysbuild** | Builds several images in one go. | [sysbuild.conf](../sysbuild.conf) adds **MCUboot**, so each build produces a bootloader *and* the app. |
| **MCUboot** | A secure bootloader with two image slots, which is what makes over-the-air updates possible. | Lives at `0x0`; the app links at `0x10000` and **cannot run without it**. |
| **RTT** | SEGGER Real-Time Transfer: a console that runs over the debug (SWD) cable instead of a UART. | Logs and the interactive shell. |

**How the source is organised.** [src/main.c](../src/main.c) calls one `*_init()` function per
module. Each module (`src/sensors/`, `src/cs/`, `src/ble/`, …) is self-contained, with its own
`CMakeLists.txt` and `Kconfig`, and can be switched off with its `CONFIG_APP_*` symbol. The
README's [Repository structure](../README.md#repository-structure) table lists every module.

---

## 3. What you need

### Hardware

| Item | Notes |
|------|-------|
| **nRF54L15 Tag** | Two are needed for the Channel Sounding (distance) demo; one is enough for everything else. |
| **[nRF54L15 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF54L15-DK)** | **This is your programmer.** The Tag has no USB or debugger of its own. It plugs into the DK's **DEBUG OUT** header, and the DK's on-board J-Link programs it. |
| USB cable for the DK | |
| **Power for the Tag** | The Tag is **not** powered through DEBUG OUT. Use **either** a CR2032 coin cell **or** 3.3 V from the DK. See [section 7](#7-connect-the-hardware-and-flash). |
| Phone (iOS or Android) | With [nRF Connect for Mobile](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile) (general Bluetooth LE explorer) and [nRF Connect Device Manager](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-Device-Manager) (shell over Bluetooth and firmware updates). |

### Software

| Tool | Notes |
|------|-------|
| [Visual Studio Code](https://code.visualstudio.com/) | The recommended IDE. Nordic's extension handles most of the setup. |
| [nRF Connect for VS Code Extension Pack](https://marketplace.visualstudio.com/items?itemName=nordic-semiconductor.nrf-connect-extension-pack) | Installs and manages the SDK and toolchain; build, flash, debug and RTT terminal. |
| [SEGGER J-Link Software](https://www.segger.com/downloads/jlink/) | Drivers and tools for the DK's debugger, including **J-Link RTT Viewer**. Use the J-Link version that the NCS v3.4.0 installation page lists as its requirement. Older J-Link versions may not recognise the nRF54L15. |
| [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util) | Nordic's command-line utility. The VS Code extension bundles it; install it separately only if you want to work from a plain terminal. |
| git | To clone this repository. |

---

## 4. Install the SDK and tools

This is the same process as DevAcademy *Fundamentals*, lesson 1. Follow that lesson if you'd
like screenshots.

1. **Install J-Link Software** from SEGGER and accept the USB driver install.
   - *Linux:* also install the Nordic udev rules (see the
     [NCS installation page](https://nrfconnectdocs.nordicsemi.com/ncs/3.4.0/nrf/installation/install_ncs.html))
     so you can reach the DK without `sudo`.
2. **Install VS Code**, then the **nRF Connect for VS Code Extension Pack** from the
   Extensions view.
3. **Install the SDK and toolchain.** Open the **nRF Connect** panel in the left sidebar. In its
   welcome view, choose **Install SDK** and select **v3.4.0**. The extension installs the
   matching toolchain with it. The download is several GB, so allow time.
   - The version has to match. This project's [west.yml](../west.yml), CI container
     (`sdk-nrf-toolchain:v3.4.0`) and Kconfig all assume v3.4.0. A different SDK version will
     usually fail to build, and if it does build, the Channel Sounding behaviour may differ.
4. **Check the DK is visible.** Plug the DK into USB. It should appear in the extension's
   **Connected Devices** view. From a terminal, `nrfutil device list` should list it.

> **Command-line-only alternative.** `nrfutil sdk-manager` can install the same SDK and
> toolchain without VS Code. See the "command line" tab of the
> [NCS installation page](https://nrfconnectdocs.nordicsemi.com/ncs/3.4.0/nrf/installation/install_ncs.html).

---

## 5. Get the code

There are two ways to set up the code. Most people should use **A**.

### A. Clone the repo and use your installed SDK (recommended)

```sh
git clone https://github.com/onceLabs/nRF54L15-Tag-Demo.git nRF54L15TagDemo
```

This is a *freestanding* application: it lives outside the SDK and finds Zephyr through the
SDK you installed in step 4. Open the `nRF54L15TagDemo` folder in VS Code.

### B. A self-contained west workspace

This repo is also a west *manifest* repository, so it can fetch its own copy of the SDK. CI
uses this approach. It downloads only the SDK modules the app needs (about 2.8 GB rather than
the full SDK):

```sh
west init -m https://github.com/onceLabs/nRF54L15-Tag-Demo --mr main my-workspace
cd my-workspace
west update --narrow -o=--depth=1
```

The app ends up in `my-workspace/nRF54L15TagDemo/`. You still need the v3.4.0 **toolchain**
from step 4. Option B only replaces the SDK source.

---

## 6. Build

### In VS Code (using the project's build preset)

The repo includes a ready-made build configuration, the `build_cli` preset in
[CMakePresets.json](../CMakePresets.json). Choosing it fills in the board target, the
`rtt-console` snippet and the build directory, so you don't have to type them yourself.

1. In the **nRF Connect** panel, under *Applications*, select `nRF54L15TagDemo`, then
   **Add build configuration**.
2. At the top of the form, open the **CMake Preset** drop-down and choose
   **Build for nRF54l15 TAG nRF54l15 Application (build_cli)**. The form fills in:
   - **Board target:** `nrf54l15tag/nrf54l15/cpuapp`
   - **Snippets:** `rtt-console`
   - **Build directory:** `build_cli`
3. Check the fields the preset doesn't set:
   - **SDK / Toolchain:** v3.4.0 / v3.4.0
   - **System build (sysbuild):** leave it on *Build system default*. NCS enables sysbuild
     by default, and sysbuild is what adds MCUboot.
   - Leave the base configuration file as `prj.conf` and the extra Kconfig/overlay fields empty.
4. Click **Generate and Build**. The first build takes a few minutes.

> **No CMake Preset drop-down?** The extension shows it only when the application folder
> contains `CMakePresets.json`, so check that you opened the repo root. Older extension
> versions don't have the drop-down. In either case, fill in the form by hand: board target
> `nrf54l15tag/nrf54l15/cpuapp` (switch the board list to *All boards* if it isn't listed),
> snippet **`rtt-console`** (don't skip this; see [section 2](#2-concepts-in-five-minutes)),
> and sysbuild on.

See [How to set up a build configuration](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/guides/build_config.html)
in the extension docs for screenshots of this form.

### From a terminal

The toolchain is **not** on your `PATH` by default. The easiest way to get it is to open a
terminal from the extension (**nRF Connect** panel → *Open terminal*), which sets the
environment for you. To do it by hand on macOS, export the toolchain environment. Your toolchain
directory name under `/opt/nordic/ncs/toolchains/` will be a different hash:

```sh
TC=/opt/nordic/ncs/toolchains/ccc010f809
export PATH="$TC/bin:$TC/usr/bin:$TC/opt/bin:$TC/nrfutil/bin:$TC/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin:$TC/opt/zephyr-sdk/gnu/riscv64-zephyr-elf/bin:$PATH"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_SDK_INSTALL_DIR="$TC/opt/zephyr-sdk" NRFUTIL_HOME="$TC/nrfutil/home" ZEPHYR_BASE=/opt/nordic/ncs/v3.4.0/zephyr
```

Then, from the repo root:

```sh
west build -b nrf54l15tag/nrf54l15/cpuapp -S rtt-console -d build_cli .
```

- `-b`: the board target. `-S rtt-console`: the snippet. `-d build_cli`: the build directory,
  the same one the preset uses, so VS Code and the terminal share one build. `.`: the app source.
- **Pristine rebuild** (after editing `CMakeLists.txt`, `Kconfig` files, board files or
  `sysbuild.conf`): add `-p always`. Put `always` in explicitly. A bare `-p`/`--pristine`
  placed before `.` swallows the `.` as its argument and the command fails.
- **Or use the preset**, so the terminal build matches VS Code exactly:

  ```sh
  west build -d build_cli . -- --preset=build_cli
  ```

  Everything after `--` is passed to CMake. Because `-b` isn't given, west prints
  `WARNING: This looks like a fresh build and BOARD is unknown`. You can ignore it, because the
  preset supplies the board (check for `-- Board: nrf54l15tag` in the output).

### Check your build

A successful build ends with two memory reports, one per image. Your numbers will differ
slightly from these:

```
Memory region         Used Size  Region Size  %age Used
           FLASH:       52164 B        62 KB     82.16%      <- MCUboot
Memory region         Used Size  Region Size  %age Used
           FLASH:      458508 B     712560 B     64.35%      <- application
```

Then check that **`build_cli/domains.yaml` lists both `mcuboot` and `nRF54L15TagDemo`**. If it
lists only the app, the build directory is stale; see
[Troubleshooting](#12-troubleshooting). The useful outputs are:

| File | What it's for |
|------|---------------|
| `merged_nrf54l15tag_nrf54l15_cpuapp.hex` | MCUboot + app in one file: a full flash over the debugger |
| `nRF54L15TagDemo/zephyr/zephyr.signed.bin` | The signed app on its own: the **over-the-air update** payload |
| `dfu_application.zip` | The same update, packaged for nRF Connect Device Manager |

---

## 7. Connect the hardware and flash

### Wiring

1. Plug the Tag into the nRF54L15 DK's **DEBUG OUT** header. The DK's debugger is now routed
   to the Tag instead of the DK's own chip.
2. **Power the Tag one way only:**
   - **Coin cell:** insert a CR2032 into the Tag. *Or:*
   - **3.3 V from the DK:** set the DK's VDD to 3.3 V and fit a jumper from any DK `VDDIO` pin
     to the `VDD SWD0` pin.

> **Never do both.** Don't apply 3.3 V to `VDD SWD0` (or the Tag's VDD in general) while a coin
> cell is inserted. Source: the
> [board documentation](https://docs.zephyrproject.org/latest/boards/nordic/nrf54l15tag/doc/index.html#programming-and-debugging),
> which includes a photo of the setup.

### Flash

- **VS Code:** *Actions* → **Flash**. See
  [How to flash a build configuration](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/guides/build_config.html).
- **Terminal:** `west flash -d build_cli`

Both write **MCUboot and the app** in the right order (MCUboot at `0x0`, app at `0x10000`).

> Don't flash `zephyr.signed.bin`, `zephyr.elf` or `mcuboot.hex` on their own with a J-Link
> tool. The app needs MCUboot underneath it. If you're using a programming tool other than
> `west flash`, use the merged hex (or the `factory.hex` from a GitHub Release).

---

## 8. Talk to the tag over RTT

All logs and the interactive shell run over RTT, through the same DK debugger connection.

**In VS Code:** in the **Connected Devices** view, expand the DK and click the RTT (terminal)
icon. See [How to connect using RTT](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/guides/bd_work_with_boards.html).

**Or with J-Link RTT Viewer** (installed with J-Link Software):

| Setting | Value |
|---------|-------|
| Connection | USB |
| Target device | `nRF54L15_M33` |
| Interface / speed | SWD / 4000 kHz |
| RTT control block | Auto detection |

Reset the tag (or reconnect) and you should see something like:

```
<inf> main: nRF54L15 Tag Demo starting
...
<inf> main: nRF54L15 Tag Demo ready
```

followed by periodic sensor readings. Press Enter to get a prompt and try:

```
help                 # every command available
cs status            # Channel Sounding role / mode / running
sensor status        # sensor rates, ranges, stationary state
sensor log all off   # quiet the 1 Hz sensor lines
log disable app_sensors
log enable dbg app_cs
```

The full command reference is in the README:
[Runtime control (RTT shell)](../README.md#runtime-control-rtt-shell).

---

## 9. Check that it works

1. **Bluetooth:** on your phone, open *nRF Connect for Mobile*, scan, and find
   **`nRF54L15 Tag`**. Connect and open **Device Information → Firmware Revision**. It should
   match [VERSION](../VERSION) (e.g. `0.0.16+0`). This is the quickest way to confirm which
   build a tag is running.
   - The tag switches to a non-connectable iBeacon frame for half of every 30 s cycle. If it
     won't connect, or doesn't show its name, wait up to 30 s and try again.
2. **Sensors:** read or subscribe to **Environmental Sensing** (temperature, humidity,
   pressure). Breathe on the board and watch humidity rise.
3. **Channel Sounding (needs two tags):** each tag boots with its LED off. Press the button
   **once** on tag A (solid **blue** = reflector) and **twice** on tag B (solid **white** =
   initiator). Tag B starts ranging and its LED shows distance by colour and blink rate.
   This can take up to about 30 s, because the reflector can't be found during its iBeacon
   half-cycle. Or use the shell: `cs role reflector` / `cs start` on one tag,
   `cs role initiator` / `cs start` on the other, and watch the `distance` lines on the
   initiator's RTT.

More demos (motion streaming, throughput, remote shell) are covered in the README's
[Functionality & how to demo](../README.md#functionality--how-to-demo) and in
[DEMO_GUIDE.md](DEMO_GUIDE.md).

---

## 10. Updating over Bluetooth (OTA)

Once a tag has MCUboot on it (from the flash in section 7), you can update it without the DK:

1. Build, then copy `build_cli/nRF54L15TagDemo/zephyr/zephyr.signed.bin` (or
   `dfu_application.zip`) to your phone.
2. In *nRF Connect Device Manager*: connect to `nRF54L15 Tag` → **Image** → select the file
   → **Test**.
3. The tag resets, MCUboot swaps in the new image, and the app confirms itself after 10 s.
   If the new image crashes before then, MCUboot rolls back to the previous image.

**Bump [VERSION](../VERSION) before an OTA build**, so you can tell the two images apart. The
full procedure, the `mcumgr` CLI equivalent, and the **security caveat** (builds are signed
with MCUboot's public development key) are in the README:
[Firmware update over BLE (OTA)](../README.md#firmware-update-over-ble-ota).

---

## 11. Your first change

Two small changes to show how configuration and code changes flow through a build.

### A configuration change (Kconfig)

[prj.conf](../prj.conf) ends with:

```
# Enable shorter distance demo
CONFIG_APP_UX_SHORT_DEMO=y
```

That sets the LED's distance zones to 1–3 ft. Change it to `n` to get the default
1 m / 3 m zones, then rebuild and flash. The option is defined, with help text, in
[src/ux/Kconfig](../src/ux/Kconfig). Every `CONFIG_APP_*` option is defined the same way in its
own module's Kconfig file.

To browse every option interactively instead of editing files:

- **VS Code:** *Actions* → **nRF Kconfig GUI**
- **Terminal:** `west build -d build_cli -t menuconfig` (or `-t guiconfig`)

> Changes made in menuconfig only last for that build directory and are lost on a pristine
> build. To keep a change, put it in `prj.conf`. The README's
> [Configuration reference](../README.md#configuration-reference) lists the useful tunables.

### A code change

In [src/main.c](../src/main.c), change the `"nRF54L15 Tag Demo ready"` log message, rebuild
(an ordinary incremental build is fine for `.c` changes), flash, and look for your text in the
RTT output.

### Adding to the codebase

When you add a feature, follow the existing module pattern: a new `src/<module>/` directory
with its own `CMakeLists.txt` and `Kconfig`, a `CONFIG_APP_<MODULE>` switch, and a header that
provides an empty stub when the module is disabled. Copy an existing small
module such as [src/dis/](../src/dis/) or [src/ess/](../src/ess/) as a template, add an
`add_subdirectory()` line to the top-level [CMakeLists.txt](../CMakeLists.txt) and an
`rsource` line to the top-level [Kconfig](../Kconfig), and call its `*_init()` from
[src/main.c](../src/main.c). Include other modules' headers by bare name
(`#include "cs_shared.h"`, not `"cs/cs_shared.h"`). For new Channel Sounding behaviour,
start from the NCS samples linked in [section 1](#1-learn-the-basics-first-nordic-tutorials),
which this project was built from.

---

## 12. Troubleshooting

| Symptom | Likely cause and fix |
|---------|----------------------|
| `west: command not found` | The toolchain isn't on your `PATH`. Use the extension's terminal or export the environment shown in [section 6](#from-a-terminal). |
| Board `nrf54l15tag/nrf54l15/cpuapp` not found | Wrong SDK version. The Tag board is part of NCS v3.4.0's Zephyr. Check that the build configuration uses SDK **v3.4.0**. |
| `west build: error: argument -p/--pristine: invalid choice` | A bare `-p` swallowed the next argument. Use `-p always`. |
| Flash "succeeds" but the tag is dead: no LED, no button response, silent RTT | **Stale build directory.** A build directory created before MCUboot was added to the project keeps producing a single app image with no bootloader. Delete it (`rm -rf build` / `build_cli`) and rebuild. Check that `domains.yaml` lists `mcuboot`. |
| Flash fails: no J-Link / no device found | Is the DK on USB and switched on? Is the Tag fully seated in **DEBUG OUT** and powered (coin cell or VDD jumper)? Check that `nrfutil device list` shows the DK, and update J-Link Software if the nRF54L15 isn't recognised. |
| RTT connects but shows nothing | The build is missing the `rtt-console` snippet (check the build configuration), or the tag isn't running; see the stale-build row above. Try resetting the tag while the RTT viewer is connected. |
| Phone can't find or connect to the tag | Wait up to 30 s; the tag is non-connectable during its iBeacon half-cycle. It also stops advertising while a phone or host is already connected to it. |
| Distance reads high or low by a fixed amount | Expected on new hardware. Retune `CONFIG_APP_CS_DISTANCE_OFFSET_MM` (see the README's [calibration section](../README.md#distance-calibration--accelerometer-stabilization)). |
| Initiator stays solid white for a while before ranging | Normal for up to ~30 s: the reflector is in its non-connectable iBeacon half-cycle. Set `CONFIG_APP_ADV_IBEACON=n` to turn the beacon off. |
| Initiator and reflector never start ranging | Both ends must use the same mode (`cs mode ras` on both, or `cs mode ipt` on both). See the README's [Notes & limitations](../README.md#notes--limitations). |

If you're still stuck, search or ask on [DevZone](https://devzone.nordicsemi.com/). Include
your NCS version, board target and the build or RTT output.

---

## 13. Where to go next

- [README.md](../README.md): features, shell commands, OTA, configuration reference,
  limitations.
- [docs/MOTION_SERVICE.md](MOTION_SERVICE.md) and
  [docs/THROUGHPUT_SERVICE.md](THROUGHPUT_SERVICE.md): Bluetooth protocols for companion-app
  authors.
- [docs/DEMO_GUIDE.md](DEMO_GUIDE.md): a non-technical walkthrough of the demos.
- **Releases:** CI builds every pull request and push to `main`, and publishes a GitHub Release
  for every `v*` tag. Cut releases with [scripts/release.sh](../scripts/release.sh), not by
  hand. See the README's [Releases (CI)](../README.md#releases-ci).

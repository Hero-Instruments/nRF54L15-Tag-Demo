# nRF54L15 Tag — Demo Guide (for Marketing)

A plain-language guide to what the nRF54L15 Tag demo does and how to show it off.
It's written so you can **run the demo and narrate it** without an engineer in the
room. No coding required.

---

## The 30-second pitch

> The nRF54L15 Tag is a small, low-power Nordic wireless device that can **measure the
> real-time distance between two of them over Bluetooth** — accurately, down to a few
> feet — using the new Bluetooth **Channel Sounding** technology. The same tag also
> **streams live motion** (how it's moving) and **senses its environment**
> (temperature, humidity, pressure), and everything on it can be **controlled and read
> wirelessly from a phone**. One coin-sized board, one chip, battery-friendly.

If you only say one sentence: **"Two of these tags can tell how far apart they are, in
real time, over Bluetooth — and each one is also a full motion and environmental
sensor you can talk to from your phone."**

---

## Why it matters

- **Distance you can trust.** Traditional Bluetooth can only *guess* distance from
  signal strength, which is unreliable and easy to spoof. Channel Sounding actually
  *measures* it. That unlocks secure "find my item," keyless access that only unlocks
  when you're genuinely close, warehouse/asset location, and proximity safety.
- **Real-time motion.** The tag streams high-rate accelerometer and gyroscope data —
  useful for gesture, impact/fall detection, activity tracking, and product analytics.
- **Environmental awareness.** Standards-based temperature, humidity, and pressure
  sensing for logistics, building comfort, and cold-chain monitoring.
- **All-in-one and wireless.** Ranging + motion + environment + remote control on a
  single low-power chip, with no cables — you manage it from a phone.

---

## The demos

Each demo below is written as **What you'll see → What to say → Setup at a glance.**

### 1. Two-tag distance demo (no PC) — the headline

**What you'll see.** Two tags. Press the button on each to pick a role; one becomes the
"anchor," the other the "seeker." The seeker's RGB LED then shows how far apart they
are: **green = close, yellow = mid-range, red = far**, and it **blinks faster the closer
they get**. Walk the tags together and apart and the light reacts live.

**What to say.** *"These two tags are measuring the real distance between them over
Bluetooth — no phone, no cloud, no setup. The light turns green and blinks faster as
they get closer. This is Bluetooth Channel Sounding: actual distance measurement, not a
signal-strength guess."*

**Setup at a glance.** Power on both tags. On tag A press the button once (anchor). On
tag B press twice (seeker). Move them toward and away from each other. A close-range
mode can tighten the reaction to a **1–3 foot** span for tabletop demos.

### 2. Live distance on a screen

**What you'll see.** With the tags ranging, connect a phone (or a small screen device)
to the seeker tag. It reads back the live distance in **feet and inches**, updating as
you move — a clean number to put on a slide or screen next to the blinking LED.

**What to say.** *"And we can pull that live distance onto any screen — here it is in
feet and inches, updating in real time while it keeps ranging."*

**Setup at a glance.** Open a Bluetooth connection from the phone to the tag and read
the distance value — no pairing step required.

### 3. Real-time motion streaming

**What you'll see.** Pick up a tag and move it; a companion app plots the
accelerometer and gyroscope in real time — tilt, shake, rotation all show up instantly.

**What to say.** *"Every tag is also a high-rate motion sensor. This is live accel and
gyro streaming over Bluetooth — the kind of data behind gesture control, fall
detection, and activity tracking."*

**Setup at a glance.** Connect the companion app, turn on motion streaming, and move the
tag. (Technical details for whoever builds the companion app live in
[MOTION_SERVICE.md](MOTION_SERVICE.md).)

### 4. Environmental sensing

**What you'll see.** Connect a phone using a standard Bluetooth scanner app; the tag
reports **temperature, humidity, and pressure**. Breathe on it and watch humidity and
temperature move.

**What to say.** *"The tag also reports its environment over a standard Bluetooth
service — so it works with off-the-shelf apps, no custom software needed."*

**Setup at a glance.** Scan for **"nRF54L15 Tag"** in a BLE app, connect, and read the
Environmental Sensing values.

### 5. Remote control over Bluetooth

**What you'll see.** From a phone (Nordic's **nRF Connect Device Manager** app) you can
send commands to the tag wirelessly — switch roles, start/stop ranging, reconfigure
sensors — without touching it or plugging in anything.

**What to say.** *"Everything you'd normally do at a desk with a cable, we can do from a
phone over Bluetooth — configure and manage the device in the field."*

**Setup at a glance.** Connect with nRF Connect Device Manager → open the shell/terminal
→ send a command (e.g. ask for the current distance or status).

### 6. Firmware update over the air

**What you'll see.** Using the same **nRF Connect Device Manager** app, you push a new
firmware build to the tag over Bluetooth. The tag reboots into the new version — no
cable, no programmer. If the new firmware were faulty, the tag would notice and put the
old version back on its own.

**What to say.** *"These ship as sealed units with no exposed port. We update them over
Bluetooth, and the device rolls itself back if an update goes wrong — so a bad update
can't strand hardware in the field."*

**Setup at a glance.** Connect with nRF Connect Device Manager → **Images** → pick the
new firmware file (`dfu_application.zip` or `zephyr.signed.bin` from the build) →
**Test**. Watch it upload, then reset into the new version. Ranging pauses during the
upload; press the button afterwards to pick the role again.

> Nice detail to mention: every image is cryptographically **signed**, and the
> bootloader refuses anything that doesn't verify.

---

## Talking points & differentiators

- **Measured, not guessed, distance** — Channel Sounding vs. old signal-strength
  estimates.
- **Two ways to range, built in** — supports both Bluetooth ranging approaches
  ("RAS" and "IPT"), switchable live, so it interoperates broadly.
- **Fast updates** — uses Bluetooth's newest sub-7.5 ms connection timing for a snappy,
  real-time feel.
- **A whole sensor suite in one** — precise ranging *plus* motion *plus* environment on
  a single low-power chip.
- **No cables, no PC** — the two-tag demo runs standalone on a button and an LED;
  everything else is reachable from a phone.
- **Standards-friendly** — uses the standard Bluetooth Environmental Sensing service, so
  it plays nicely with existing tools.
- **Updatable in the field** — signed firmware updates over Bluetooth, with automatic
  rollback if an update fails to run.

---

## Glossary (say it in plain English)

| Term | Plain-English meaning |
|------|------------------------|
| **Channel Sounding** | New Bluetooth feature that *measures* the real distance between two devices. |
| **RAS / IPT** | Two built-in methods for doing that ranging; the demo can switch between them. Just say "two ranging modes." |
| **Initiator / Reflector** | The two roles in a ranging pair — think "seeker" and "anchor." |
| **Central / Peripheral** | Bluetooth roles for who connects to whom; not worth explaining on stage. |
| **ESS (Environmental Sensing Service)** | The standard Bluetooth way the tag shares temperature/humidity/pressure. |
| **RTT** | A debug console engineers use over the programming cable; not needed for the phone demos. |
| **SMP / MCUmgr** | The mechanism that lets a phone send commands to the tag over Bluetooth. |
| **OTA / DFU** | Updating the tag's firmware wirelessly ("over the air" / "device firmware update"). |
| **MCUboot** | The small program that starts the tag and decides which firmware version to run. |

---

## At-a-glance demo kit

- **2 tags** — required for the distance demo (one anchor, one seeker).
- **A phone** — for live distance on a screen, environmental readings, and remote
  control (Nordic **nRF Connect** and **nRF Connect Device Manager** apps, both free).
- **Optional companion app** — for the real-time motion plots.
- **A firmware file** — if you want to show the over-the-air update
  (`dfu_application.zip` from a build).
- **The one-button flow** — press the button to cycle a tag through **Off → Anchor →
  Seeker**; the LED tells you which mode it's in (off / solid blue / white then
  color-by-distance).

*The tags advertise as **"nRF54L15 Tag"** — that's what you'll look for when scanning
from a phone.*

# nRF54L15 Tag — Throughput Service BLE Protocol

Self-contained specification for the custom BLE **Throughput Service**, used to
measure real BLE transfer rate on the nRF54L15 Tag. This document is everything a
client needs to run a measurement — no access to the firmware source is required.

Audience: developers (or an AI assistant) building a test harness — e.g. a Python
(bleak) host, a VS Code extension, or the Hero Workbench MCP.

---

## 1. Overview

The service measures the four GATT transfer paths that behave differently on air,
all through a **single Data characteristic**:

| # | Direction | Mechanism | Acknowledged? | Expected relative speed |
|---|-----------|-----------|---------------|-------------------------|
| 1 | host → tag | Write Without Response | no | fastest uplink |
| 2 | host → tag | Write (with response) | yes, per packet | much slower — one round trip per packet |
| 3 | tag → host | Notification | no | fastest downlink |
| 4 | tag → host | Indication | yes, per packet | much slower — one confirmation per packet |

Paths 1 and 2 are driven **by the host** (just write, repeatedly). Paths 3 and 4
are driven **by the tag** and must be started by writing the Control
characteristic.

- The Tag is a **BLE peripheral**; your harness is the **central**.
- **Find the device by name:** it advertises the complete local name
  **`nRF54L15 Tag`**.
- The **Throughput Service UUID is 128-bit and is NOT in the advertising data.**
  Connect first, then discover.

---

## 2. Connection setup

Throughput is dominated by link parameters, so negotiate them before measuring
and record them alongside every result — a number without its link parameters is
meaningless.

- **ATT MTU** — request an exchange; the tag supports up to **498**.
- **LE 2M PHY** — request a PHY update.
- **Data Length Extension** — request 251-octet PDUs. **Verify this was actually
  granted.** If the link reports 27 octets, every large ATT packet fragments
  ~19 ways and results will be far below what the link can really do.
- **Connection interval** — at the start of a device→host run the tag requests a
  short interval (default 7.5 ms). The central ultimately decides.

---

## 3. Service & characteristics

All UUIDs are 128-bit with the base suffix `-9b0f-4a3e-8b1a-2f9c0d5e7a10` (the
same vendor base as the Motion Service).

| Role | UUID | Properties |
|------|------|------------|
| **Throughput Service** | `f0de1c00-9b0f-4a3e-8b1a-2f9c0d5e7a10` | Primary service |
| **Data** | `f0de1c01-9b0f-4a3e-8b1a-2f9c0d5e7a10` | WriteWithoutResponse, Write, Notify, Indicate (+ CCCD) |
| **Control** | `f0de1c02-9b0f-4a3e-8b1a-2f9c0d5e7a10` | Read, Write |

### Data characteristic

- **Writing to it** (paths 1 & 2) is the uplink test. Any length from 0 up to
  `ATT_MTU - 3` is accepted; **contents are ignored**. The two write types are
  counted separately.
- **Subscribing to it** (paths 3 & 4) prepares the downlink test. Subscribe for
  *notifications* to test path 3, or for *indications* to test path 4 — the
  method you request in the Control write must match what you subscribed for, or
  the start is rejected.
- Prepared ("long") writes are rejected — they aren't one of the measured paths.

### Payload contents

Payloads are a **raw incrementing byte filler with no header**, so a run measures
the transport ceiling with zero protocol overhead. There is no sequence number:
detect loss by comparing the two ends' counters (what you sent vs `rx_*`; or
`tx_packets` vs what you received).

---

## 4. Control characteristic — write (exactly 10 bytes, little-endian)

| Offset | Size | Field | Values |
|--------|------|-------|--------|
| 0 | 1 | `op` | `0` = STOP, `1` = START_TX, `2` = RESET_STATS |
| 1 | 1 | `method` | `0` = notify, `1` = indicate |
| 2 | 1 | `limit_kind` | `0` = free-run, `1` = duration (ms), `2` = byte count |
| 3 | 1 | reserved | write `0` |
| 4 | 2 | `payload_len` | bytes per packet; `0` = auto (max for current MTU) |
| 6 | 4 | `limit` | milliseconds or bytes, per `limit_kind` |

A write of any other length is rejected with `Invalid Attribute Length`.

- `START_TX` zeroes the **TX** counters and begins the run.
- `RESET_STATS` zeroes **both** directions. Rejected while a run is in progress.
- `STOP` ends a run early. Always safe to send.

Rejected (with `Write Request Rejected` or `Value Not Allowed`) when: a run is
already in progress; nothing is subscribed; the subscription type doesn't match
`method`; `payload_len` exceeds 495; or `limit_kind` is non-zero with `limit = 0`.

### Examples

```
Notify for 5 seconds at max payload:
  01 00 01 00  00 00  88 13 00 00      (op=1 method=0 kind=1 len=0 limit=5000)

Indicate 100 000 bytes at 100-byte packets:
  01 01 02 00  64 00  a0 86 01 00

Notify free-run at 244-byte packets:
  01 00 00 00  f4 00  00 00 00 00

Stop:
  00 00 00 00  00 00  00 00 00 00
```

---

## 5. Control characteristic — read (44 bytes, little-endian)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | `state` — `0` = idle, `1` = running |
| 1 | 1 | `method` of the current/last run |
| 2 | 2 | `payload_len` actually in use |
| 4 | 4 | `tx_packets` |
| 8 | 4 | `tx_bytes` |
| 12 | 4 | `tx_errors` |
| 16 | 4 | `tx_elapsed_ms` |
| 20 | 4 | `rx_cmd_packets` — writes **without** response |
| 24 | 4 | `rx_cmd_bytes` |
| 28 | 4 | `rx_req_packets` — writes **with** response |
| 32 | 4 | `rx_req_bytes` |
| 36 | 4 | `rx_elapsed_ms` |
| 40 | 2 | `att_mtu` — current negotiated ATT MTU |
| 42 | 2 | `max_payload` — `att_mtu - 3` |

This is longer than a default-MTU read; if you have not negotiated a larger MTU
your stack will fetch it with a Read Blob continuation automatically.

`rx_elapsed_ms` is measured from the **first** write to the **most recent** one,
so `rx_bytes / rx_elapsed_ms` is a true rate rather than an average since boot.

Throughput in **kbit/s** = `bytes * 8 / elapsed_ms`.

---

## 6. Worked procedure

**Uplink (paths 1 & 2)**

1. Connect; negotiate MTU / PHY / DLE.
2. Write Control `op=2` (RESET_STATS).
3. Write the Data characteristic N times with your chosen length and write type.
4. Read Control; compute `rx_cmd_bytes / rx_elapsed_ms` (or `rx_req_*`).
5. Compare `rx_cmd_packets` against N to find loss.

**Downlink (paths 3 & 4)**

1. Connect; negotiate MTU / PHY / DLE.
2. Subscribe to Data — notifications for path 3, indications for path 4.
3. Write Control `op=1` with the matching `method` and your limit.
4. Wait for the run to end (or poll `state` until it reads `0`).
5. Read Control; compute `tx_bytes / tx_elapsed_ms`. Compare `tx_packets` with
   how many packets you actually received to find loss.

**Sweeping packet size.** Repeat a run with `payload_len` of 20, 100, 244 and 495
to plot rate against packet size — the knee typically sits at the DLE boundary.

---

## 7. Interpreting results

- `tx_errors` should stay **0** on a healthy link. Non-zero means the stack
  refused sends or an indication went unconfirmed.
- **Indication and write-with-response will be dramatically slower** than their
  unacknowledged counterparts — that is the protocol, not a fault. Each costs a
  round trip, so their rate is bounded by the connection interval.
- If notification throughput is far below expectation, check `att_mtu` in the
  stats block and confirm DLE was granted. An MTU of 498 over 27-octet PDUs
  performs far worse than the MTU alone suggests.
- Channel Sounding, IMU streaming and an in-flight DFU all contend for radio
  time. Measure with the tag idle for a clean baseline.

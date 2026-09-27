# MuonP4 Bluetooth protocol, version 4

All integers are little-endian. Channel order: **CH01, CH02, CH12, CH012, GPIO6, GPIO5, GPIO16**. The byte layout is implemented in `../main/telemetry_protocol.c` and decoded by `../MuonMonitor/Shared/Telemetry.swift`.

## Discovery and advertising

Complete name: `MuonP4`. Primary service UUID: `73B47A10-6F6E-4D75-9A50-4D756F6E5034`.

Primary legacy advertising data contains flags (3 bytes including AD header), the 128-bit UUID (18), and complete local name (8): **29/31 bytes**. No scan response is required to discover the UUID or name. A 350 ms scannable advertising window runs about every 15 seconds with a 100 ms interval inside the window. Advertising is connectable while no phone is connected, and non-connectable while the one permitted connection is active.

Manufacturer data is carried in **SCAN_RSP**, so a scanner must perform an active scan. It is 29 bytes plus its two-byte AD header, exactly 31 bytes. iOS background discovery must not depend on obtaining it.

| Manufacturer data offset | Bytes | Value |
|---|---:|---|
| 0 | 2 | Experimental company ID FFFF |
| 2 | 2 | ASCII MP |
| 4 | 1 | Version 4 |
| 5 | 1 | Flags below |
| 6 | 4 | Minute sequence |
| 10 | 4 | Signed detector temperature, milli-°C; INT32_MIN invalid |
| 14 | 3 | Pressure, Pa; FFFFFF invalid |
| 17 | 4 | Latest completed minute CH01 |
| 21 | 4 | Latest completed minute CH02 |
| 25 | 4 | Latest completed minute CH12 |

The three advertisement counts are minute counts, not cumulative totals. A passive scan sees the primary name/UUID but not this response. nRF Connect/Scanner can display the name and raw manufacturer bytes; it will not automatically assign custom units/labels to this experimental payload. Use the connected characteristic for the exact full dataset and reboot-safe cumulative recovery.

## Connected characteristics

All share the service suffix `-6F6E-4D75-9A50-4D756F6E5034`:

| Prefix | Characteristic | Operations |
|---|---|---|
| 73B47A11 | Full telemetry | Read, Notify |
| 73B47A12 | Command JSON | Write with response; encrypted link required |
| 73B47A13 | Command result JSON | Read, Notify; encrypted reads |

Connections explicitly request Just Works pairing and exchange encryption/identity keys for bonding. The initial connection requests a short interval while pairing (up to 30 seconds), then returns to the quiet interval after encryption succeeds. Telemetry remains readable without encryption; detector controls retain encrypted-link requirements. This provides link encryption, not owner authentication or MITM protection.

Telemetry notifications are **8-byte heartbeat/change tokens**, not truncated telemetry: `M N 04 flags sequence:u32`. On notification, read the complete telemetry characteristic. The preferred ATT MTU is 247; smaller-MTU long reads use a stable cached snapshot. A connection can still generate keepalive traffic between these 15-second notifications.

### 160-byte telemetry value

| Offset | Bytes | Field |
|---|---:|---|
| 0 | 2 | ASCII MP |
| 2 | 1 | Version 4 |
| 3 | 1 | Flags |
| 4 | 8 | Random boot/session ID |
| 12 | 4 | Completed-minute sequence |
| 16 | 8 | Sample-end uptime, milliseconds |
| 24 | 8 | Cumulative valid physics exposure, milliseconds |
| 32 | 8 | Sample-end Unix epoch; meaningful only with clock flag |
| 40 | 4 | Actual minute integration duration, milliseconds |
| 44 | 4 | Signed temperature, milli-°C; INT32_MIN invalid |
| 48 | 4 | Pressure, Pa; UINT32_MAX invalid |
| 52 | 2 | Current status bits |
| 54 | 1 | Current HV byte |
| 55 | 1 | Reserved, zero |
| 56 | 56 | Seven uint64 cumulative physics counts |
| 112 | 28 | Seven uint32 latest completed minute counts |
| 140 | 6 | P4 base MAC bytes, device identity |
| 146 | 8 | Current uptime, milliseconds |
| 154 | 6 | Reserved, zero |

Flags: bit 0 sample exists; bit 1 completed sample is physics-valid; bit 2 completed sample's sensor reading is valid; bit 3 sample clock was set; bit 4 Wi-Fi is currently stopped/deinitialized; bit 5 HV currently settled; bit 6 current physics-ready state; bit 7 shutdown transition pending/in progress.

Status: bit 0 FPGA ready; bit 1 SD mounted; bit 2 most recent count-file write/flush succeeded; bit 3 a Bluetooth control/download command occurred within 30 seconds. “Mounted” alone is not a write guarantee. The sensor reading must be fresh at minute completion; the packet also exposes sample age via uptime. Environmental fields are not refreshed independently of the completed minute.

Cumulative totals include **only completed valid physics intervals**, including intervals missed by the phone; partial intervals discarded at transitions are excluded. Compare totals only within matching device ID and boot ID and with increasing cumulative exposure. Divide count deltas by exposure deltas, not phone wall-clock elapsed time. Gaps do not preserve missing per-minute environmental readings. The phone stores uint64 values exactly in its typed decoder; external JSON consumers must avoid converting large integers through lossy IEEE-754 doubles.

## Commands and downloads

Write one UTF-8 JSON object, at most 240 bytes, with a positive uint32 `id` and `op`. The application sends one operation at a time. The ATT write response acknowledges queuing, not successful hardware completion. The separate result is at most 511 UTF-8 bytes and includes matching `id`, boolean `ok`, and an `error` string on failure. Result notifications contain ASCII `OK`, telling the client to read the result characteristic. The app also polls after write completion, so a lost notification does not require retransmitting a hardware command. It uses a 45-second response timeout and **does not automatically retry mutations**.

| op | Arguments | Result / action |
|---|---|---|
| status | none | Clock, Wi-Fi state, settling, DAC codes, run label, active filenames |
| counts | none | Non-destructive live, last, and all-record totals; totals encoded as decimal strings |
| environment | none | Latest temperature, pressure, humidity, reading age and validity |
| time | epoch: integer seconds | Set clock and repair unsynced run filenames |
| label | label: string | Sanitize and set the run filename label |
| start_physics | none | Queue normal Wi-Fi/HV transition |
| wifi_keep | enable: 0 or 1 | Keep AP on or allow idle shutdown; cannot resurrect a stopped AP |
| hv | value: 0..255 | HV byte; zero switches HV off, nonzero settles before counting |
| dac | channel: 0..7, value: 0..1023 | Set one 10-bit DAC channel |
| dac_startup | none | Restore startup DAC profile |
| fpga | none | HV off, embedded FPGA configuration, startup DAC, startup HV + settle |
| files | index: 0-based integer | One safe log-file name, size, mtime, or eof |
| file | name, offset; optional limit | Up to 240 raw bytes encoded as base64, next offset, fixed limit, eof |
| ram | optional sequence | Without sequence: CSV header + oldest/latest retained sequence. With sequence: exact CSV row or explicit expired-record error |

For `file`, the first result establishes a fixed byte limit. Supply that limit on every following chunk so downloading a currently growing log terminates at a coherent prefix. Names are restricted to safe CSV/log basenames; paths and directory traversal are rejected. The app validates every returned offset, decodes base64, writes a protected partial file and renames it only after completion. A rename/removal on the detector during transfer produces an error rather than silently selecting a different file.

No command provides P4 firmware flashing, arbitrary memory access, file deletion or a Wi-Fi-on transition after shutdown. FPGA programming uses the already embedded bitstream and is an explicit operator control, just as in the web UI.

## Schema-5 extended records

The version-4 wire layout remains unchanged. [Schema 5](RECORD-SCHEMA-5.md) adds identity exchange, indexed history recovery, clock observations and acknowledged GPS companion writes through the existing encrypted control service. The `ram` CSV command now uses `offset` / `next` / `eof` pagination.

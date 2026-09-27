# Auditable records and location recovery (schema 5)

The detector runs and writes measurements without a phone. iOS 2.1 adds resumable record recovery and sends available phone locations back to the SD card. GPS is optional; losing Bluetooth, denying location permission, or leaving the phone behind does not stop the detector.

## Hardware and measurement limits

This deployment is **v2.2**, [hardware commit 677fb1b10999e2f20cf2f02e3deb845203625162](https://github.com/tharinduudu/gLOWCOST-2v2-mppcInterface-/commit/677fb1b10999e2f20cf2f02e3deb845203625162), with **three connected paddles** and the fourth analog channel unconnected. All eight DAC commands are retained, including those serving the unused channel.

The seven existing FPGA outputs are preserved in order: CH01, CH02, CH12, CH012, GPIO6, GPIO5, GPIO16. The last three are raw inputs associated with the singles. The supplied bitstream has no verified RTL/counter specification in these repositories: `coincidence_semantics=unverified`. Do **not** assume pair counts are exclusive of triples or derive exclusive subsets by subtraction. `fourfold` is empty, not zero. No FPGA changes are made.

`hv_byte` is the successfully commanded MAX1932 byte. `hv_enabled` means it is nonzero; it is not a voltage measurement. `hv_measured_v` is empty because no calibrated ADC readback is established. Commanding zero is not proof that the physical output is zero volts. `fpga_sha256` identifies the embedded image precisely; its human-readable version is `unknown`.

## Files on SD

| File | Purpose |
|---|---|
| `muon_*.csv` | Usual named/rotated count log with appended schema-5 columns |
| `env_*.csv` | Existing five-minute environmental averages |
| `records_<boot>.csv` | Immutable per-boot copy of completed measurement rows; name never changes when clock/label changes |
| `records_<boot>.idx` | Binary offset/length index used by Bluetooth recovery; keep it with its CSV |
| `events_<boot>.csv` | Monotonic time, UTC, event, channel and command value for DAC, HV, Wi-Fi and time changes |
| `locations_<boot>.csv` | Phone GPS and manual-stationary observations keyed by boot and sequence |

FPGA configuration remains embedded in the firmware, not on the SD card. The card does not need startup files. The two count CSVs deliberately duplicate measurements; de-duplicate by device, boot and sequence if processing both. Keep companion/index files when copying a card. FAT32 and flushes do not guarantee survival of arbitrary power loss; recovery detects missing/truncated rows rather than fabricating measurements.

## Appended measurement columns

Existing columns and their order are retained. Parse by header name.

| Fields | Meaning |
|---|---|
| `schema`, `device_id`, `boot_id`, `sequence` | Schema 5, base-MAC identity, random boot ID and increasing completed-record number. Boot/sequence are authoritative across clock jumps. |
| `git_sha`, `build_dirty`, `build_utc` | Firmware source revision, tracked-worktree modification flag and CMake configuration UTC. Use `idf.py reconfigure build` after changing commits. |
| `fpga_sha256`, `fpga_version`, `hardware_revision`, `hardware_sha` | Embedded FPGA content hash and fixed deployment identity |
| `epoch`, `iso`, `time_set` | Detector UTC seconds, ISO UTC ending in `Z`, validity of clock synchronization. Before sync, UTC is unset; counting still works. |
| `start_uptime_ms`, `uptime_ms`, `interval_ms` | Monotonic integration start/end and duration in milliseconds |
| `temp_c`, `pressure_hpa`, `humidity_pct`, `env_valid` | Fresh end-of-minute BME280 snapshot; unavailable values are blank |
| `dac0_start`…`dac7_start`, `dac0`…`dac7` | DAC codes at integration start/end, decimal integers; intervening successful changes are in the events file |
| `hv_start`, `hv_byte`, `hv_enabled`, `hv_settled` | Commanded HV and counting-settle state |
| `wifi_on`, `wifi_off`, `ble_connected` | State at record completion |
| `ble_advertising` | At least one successful advertising-burst start during the interval; not a continuously-on flag |
| `ble_notifications` | Successful telemetry notification submissions during the interval, not receiver acknowledgements or RF packet counts |
| `quality_flags` | Bitmask below |
| `sync_source`, `sync_epoch_ms`, `sync_uptime_ms` | Last explicit clock setting: 0 unknown, 1 phone BLE, 2 browser; target UTC and monotonic time of setting |
| `clock_offset_ms` | Last applied clock correction, target minus previous detector clock, in milliseconds. Blank if the previous clock was unset. This is not a calibrated drift model. |
| `clock_uncertainty_ms` | Blank when unknown. BLE/browser transit time and the current second-resolution time-setting command do not justify claiming millisecond accuracy. |
| `total0`…`total6` | Exact uint64 cumulative raw counts in completed records, including non-physics records |
| `physics_total0`…`physics_total6`, `physics_exposure_ms` | Exact uint64 totals/exposure for completed valid physics intervals only |
| `rate0`…`rate6` | Minute counts × 60000 / actual interval milliseconds |

Cumulative values reset on boot. Raw totals exclude discarded partial windows, so they are not continuous free-running FPGA counters. Preserve uint64 decimal values as integers or strings; do not round them through an IEEE-754 double.

Quality bits: **1** DAC change, **2** automatic temperature compensation, **4** HV transition, **8** Wi-Fi transition, **16** time synchronization, **32** detected software counter saturation. Overflow invalidates physics. HV/Wi-Fi/manual-DAC changes discard the partial integration; their events may therefore appear only in the event file. A bounded automatic temperature-compensation update retains the integration, sets bits 1 and 2, and records start/end codes. `physics_valid` describes the operating state, not proof of absence of all interference. This does not detect FPGA-internal overflow or missed GPIO edges.

## Resumable Bluetooth commands

The existing **version-4 160-byte telemetry and advertising layout are unchanged**. Extended fields use the encrypted JSON control service. Requests carry the existing integer `id`; responses carry `id` and `ok`. Boot IDs are decimal **strings**, not JSON floating-point numbers.

| Request | Response / behavior |
|---|---|
| `{"op":"hello","app":"2.1"}` | `schema:5`, `wire:4`, firmware SHA, dirty flag, build UTC, FPGA hash, hardware, device, boot, latest sequence; echoes the received app version |
| `{"op":"clock"}` | Detector `utc_ms`, monotonic `uptime_ms`, `time_set`; read-only |
| `{"op":"journal","boot":"123","seq":0}` | `last`: highest index slot, which can contain gaps after failed writes |
| `{"op":"record","boot":"123","seq":1,"offset":0}` | Up to 200 CSV bytes as base64 `data`, `next`, complete-row `length`, FNV-1a-32 `hash`, and `eof` |
| `{"op":"record","boot":"123","seq":0,"offset":0}` | That journal's original CSV header, with the same chunk/checksum format |

The app validates device/boot/sequence, row length and checksum, then saves each whole row. Interrupted transfers retry at most that row; completed rows are the checkpoint. Missing/corrupt indexed records are deferred for retry so subsequent records can recover. It scans journal files to include previous boots and matches known records by identity rather than UTC. Historical rows with no trustworthy UTC/phone anchor remain available for explicit import; they are not automatically assigned invented dates or GPS.

Firmware writes and flushes the row before its index entry. The checksum detects transfer inconsistency, not cryptographic tampering. An SD failure may leave an unindexed row available only through full-file import. The old `ram` command is now paginated: use optional `offset`, then `next`/`eof`, rather than assuming its `csv` field contains the entire extended row.

## GPS on both phone and SD

While logging, the phone collects timestamped fixes independently of BLE and saves a local history per run. Recovery uses a nearby recorded fix (less than 120 seconds from the minute), retaining its actual fix time and accuracy. It does not claim that the detector itself measured GPS. iOS can still suspend/terminate the app or deny fixes; no historical Find My lookup, GPX import or Health workout import is used.

In **Runs → run detail → Assign stationary location**, choose a time range and decimal latitude/longitude. This fills missing locations only and also applies to later-recovered minutes in that range. It does not overwrite recorded GPS. Assigned rows explicitly say `manual_stationary`; they have no invented GPS accuracy or altitude.

Example location command (plus the normal `id`):

```json
{"op":"location","boot":"123","seq":1,"rev":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","lat":407000000,"lon":-740000000,"fix":1790500000000,"acc":1000,"alt":2000,"src":0}
```

Units: lat/lon are degrees × 10⁷; fix is UTC milliseconds; accuracy and altitude are centimeters. Accuracy `-1` and altitude `-100000000` mean unknown. `src` is 0 GPS or 1 manual. A 32-hex-digit revision identifies an immutable observation. The board requires an existing indexed measurement; it flushes the location companion before acknowledging `rev`. Identical retries do not duplicate rows; conflicting payloads with the same revision are rejected. Incomplete trailing writes are removed before appending a new observation. Measurement CSV rows are never rewritten by the phone.

The companion header is:

```csv
boot_id,sequence,revision,latitude_e7,longitude_e7,fix_epoch_ms,h_accuracy_cm,altitude_cm,location_source
```

The app queues unacknowledged locations, retries while logging and connected to the same detector, and reads matching location companions periodically. Import the measurement CSV and its location companion together to restore both. Precise coordinates now exist **on the SD card as well as the phone/export**; they are not included in public advertising. BLE control uses existing encrypted Just Works pairing, not strong owner authentication.

Phone exports retain all measurement diagnostics plus `transport=BLE|SD_backfill`, `acquisition=live|recovered`, `sd_recovered`, `location_source`, `location_revision`, and `fix_epoch_ms`. A live BLE minute enriched with SD diagnostics retains its original BLE acquisition provenance. `detector_epoch` preserves original device UTC separately from any phone-derived display timestamp; `utc_estimated=1` marks estimates. Missing historical GPS stays missing unless manually assigned.

## Validation and remaining device checks

Host tests execute production C CSV/journal code and test raw-versus-physics accumulation, saturation, uint64 values, record/path validation, duplicate/conflicting locations and partial-write retry. Swift tests import a fixture emitted by that C code, round-trip GPS and uint64 metadata, check identity across clock jumps, stationary assignment boundaries and old Codable records. Firmware and signed iOS builds are checked without installing.

The schema-5 firmware has **not been flashed as part of this change**. Physical checks still needed: disconnect/reconnect during history and GPS transfers, reboot between missing intervals, SD failure/recovery, background GPS/Live Activity, SwiftData migration on the existing phone database, and layout on the user's iPhone. Current version-4 firmware continues to work with the app's legacy SD recovery; extended diagnostics and GPS-on-SD require schema 5.

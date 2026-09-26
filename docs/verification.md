# Verification and deployment status

## Verified locally

- ESP-IDF 5.5.4 P4 build, with hosted driver 1.4.7 and remote Wi-Fi 0.14.5: passed. The application is 947,824 bytes and fits the existing 1 MiB partition with 100,752 bytes free.
- Xcode 27.0 (27A266a), iOS 27.0 SDK: simulator and unsigned real-iPhone Release builds passed for the app and Live Activity extension.
- iPhone 17 Pro simulator on iOS 26.5: native Live and Charts screens launched, captured and visually reviewed using explicitly labelled synthetic data. The screenshots in this repository contain no actual GPS measurements or detector run data.
- C-to-Swift protocol tests: all seven counters, 64-bit values, signed temperature, invalid versions/lengths, invalid sensor flags, duplicate/boot handling and count recovery across missed updates passed.
- Physics-window tests: full stable intervals, setup exclusion, transitions between polls and excessively delayed windows passed.
- Correction tests: reference factor, coefficient signs/units, CH12's opposite sign, unsupported channels and setup exclusion passed.

Run `./tests/run-host-tests.sh` from the repository root. This checks the actual C encoder against the Swift decoder when `swiftc` is available. Firmware and app builds are described in the [development guide](developer-guide.md).

The Debug-only `--demo` argument launches synthetic data without starting Bluetooth or GPS; add `--demo-charts` to open Charts. Release builds exclude the synthetic-data generator.

## Physical P4 testing — 2026-09-26

The P4 firmware was flashed and all three written-image hashes verified. The original P4 flash backup was retained; the C6 firmware and SD contents were not erased.

On the attached ESP32-P4 v1.3, the FPGA initialized successfully. The 120-second idle timeout triggered at uptime 133.139 seconds. HV was disabled at 134.639 s, Wi-Fi stopped at 134.780 s, HV restored at 137.780 s, and settling finished at 147.785 s. No reboot, watchdog, panic or brownout message appeared in the 280-second capture.

Mac Bluetooth testing verified MuonP4 discovery, compact manufacturer data, complete 160-byte connected reads, notification tokens, and continued connected reads after Wi-Fi deinitialization. Setup minutes contributed zero physics exposure. The first completed physics minute had 60,000 ms exposure; cumulative totals matched its counts. Temperature and pressure were valid. These results establish that the installed C6 supports the tested hosted-BLE path.

Two issues were found and resolved during hardware testing:

- The SD card initially failed card-command initialization and was correctly reported unavailable over Bluetooth. The user found that it had not latched in. At their request the old boot/Linux layout was replaced with one full-size FAT32 volume; after reseating, the P4 successfully initialized the SL32G card at 10 MHz. SD writes and a complete 441-byte log download subsequently passed; the downloaded CSV matched RAM rows and overlapping Bluetooth telemetry.
- The Mac's initial encrypted control write failed with ATT Insufficient Encryption. The firmware now explicitly requests security, exchanges bonding keys and keeps connection intervals short during initial pairing. Hardware retest confirmed encryption, status/count/environment commands, clock synchronization, manual Start Physics Run, file listing, chunked SD download and RAM CSV retrieval.

The final test captured an advertisement while the encrypted connection was still active. The downloaded SD file contained one setup row and two valid physics rows; their intervals were 60,000 and 60,011 ms. One initial 50-second discovery attempt did not find the device; a subsequent scan with case-normalized UUID/name matching connected successfully. A later reconnect scan also timed out. Installing the test scanner callback before starting scanning then discovered the device and passed an encrypted reconnect after a fresh boot; the test library's convenience scan registers its callback after start. Short advertising windows still do not guarantee reception. Discovery latency and background behavior still require iPhone testing.

## Physical-device work remaining

The app has not been signed/installed on a physical iPhone. Remaining checks include:

- iPhone pairing, reconnects, screen-off background updates, physical Dynamic Island/Lock Screen presentation and GPS permission/background behavior.
- Wi-Fi client hold-open/late-client cancellation.
- Disconnects during downloads and detector reboot while the phone is logging.
- BLE-induced detector noise, electrical power stability and pulse-count accuracy.

`physics_valid` means the firmware's operating-state checks passed. It does not certify that Bluetooth interference or detector temperature dependence has been experimentally excluded. Cumulative totals preserve completed valid intervals within a boot, not discarded partial windows or missing per-minute environmental samples.

## Original shutdown diagnosis

Read-only inspection and a restored-boot capture confirmed deliberate Wi-Fi shutdown followed by HV cycling, a three-second off hold and ten-second settle. The inspected installed image was an older variant with a 20-second setup window; upstream main had shortened it to 7 seconds. No repeated boot, watchdog, brownout or transport-reset message appeared in the 125-second capture. This supports the firmware shutdown diagnosis but is not an electrical supply or RF-interference measurement.

The original full P4 backup was retained privately. Device dumps, private backup manifests, raw serial captures and phone exports are not part of this repository publication. A P4 dump does not contain the separate C6 firmware or SD card.

# Verification and deployment status

## Verified locally

- ESP-IDF 5.5.4 P4 build, with hosted driver 1.4.7 and remote Wi-Fi 0.14.5: passed. The application is 947,136 bytes and fits the existing 1 MiB partition with 101,440 bytes free.
- Xcode 27.0 (27A266a), iOS 27.0 SDK: simulator and unsigned real-iPhone Release builds passed for the app and Live Activity extension.
- iPhone 17 Pro simulator on iOS 26.5: native Live and Charts screens launched, captured and visually reviewed using explicitly labelled synthetic data. The screenshots in this repository contain no actual GPS measurements or detector run data.
- C-to-Swift protocol tests: all seven counters, 64-bit values, signed temperature, invalid versions/lengths, invalid sensor flags, duplicate/boot handling and count recovery across missed updates passed.
- Physics-window tests: full stable intervals, setup exclusion, transitions between polls and excessively delayed windows passed.
- Correction tests: reference factor, coefficient signs/units, CH12's opposite sign, unsupported channels and setup exclusion passed.

Run `./tests/run-host-tests.sh` from the repository root. This checks the actual C encoder against the Swift decoder when `swiftc` is available. Firmware and app builds are described in the [development guide](developer-guide.md).

The Debug-only `--demo` argument launches synthetic data without starting Bluetooth or GPS; add `--demo-charts` to open Charts. Release builds exclude the synthetic-data generator.

## Physical-device work remains

The new firmware has **not been flashed** and the app has not been signed/installed on a physical iPhone. Compilation and simulator UI checks do not validate:

- The installed C6 image's hosted-BLE support and concurrent advertising/connections after Wi-Fi deinitialization.
- Pairing, reconnects, screen-off background updates, physical Dynamic Island/Lock Screen presentation and GPS permission/background behavior.
- Real control execution and complete/interrupted SD transfers.
- BLE-induced detector noise or pulse-count accuracy.

A later authorized trial should cover the two-minute idle timeout, late-client cancellation, manual Start Physics Run, HV settling, full-minute flags, sensor/SD failure, disconnects during downloads and a detector reboot while the phone is logging.

`physics_valid` means the firmware's operating-state checks passed. It does not certify that Bluetooth interference or detector temperature dependence has been experimentally excluded. Cumulative totals preserve completed valid intervals within a boot, not discarded partial windows or missing per-minute environmental samples.

## Original shutdown diagnosis

Read-only inspection and a restored-boot capture confirmed deliberate Wi-Fi shutdown followed by HV cycling, a three-second off hold and ten-second settle. The inspected installed image was an older variant with a 20-second setup window; upstream main had shortened it to 7 seconds. No repeated boot, watchdog, brownout or transport-reset message appeared in the 125-second capture. This supports the firmware shutdown diagnosis but is not an electrical supply or RF-interference measurement.

The original full P4 backup was retained privately. Device dumps, private backup manifests, raw serial captures and phone exports are not part of this repository publication. A P4 dump does not contain the separate C6 firmware or SD card.

# iPhone features and examples

MuonMonitor is a native SwiftUI app with four tabs: **Live**, **Charts**, **Detector** and **Settings**. It works with the protocol-v4 P4 firmware in this repository. The app and firmware compile successfully; physical Bluetooth, background GPS and Live Activity behavior still need hardware acceptance testing.

## Preview

These are native iPhone 17 Pro simulator captures using clearly labelled synthetic measurements. They demonstrate the UI, not a measured detector run.

| Live dashboard | Raw and corrected charts |
|---|---|
| ![Simulated live dashboard](assets/iphone-live-demo.png) | ![Simulated corrected chart](assets/iphone-charts-demo.png) |

The Live screen shows seven completed-minute counts and exact cumulative physics totals. The Charts screen overlays raw and corrected rates, with pressure and detector-temperature plots below. Near the reference conditions, corrections are small and the traces can overlap.

## Install and start a session

1. Open [`MuonMonitor.xcodeproj`](../MuonMonitor/MuonMonitor.xcodeproj) in Xcode.
2. Set your Apple development team for both **MuonMonitor** and **MuonLive**. If changing bundle identifiers, keep the extension identifier under the app identifier.
3. Build/run on an iPhone with iOS 17 or later. Simulator BLE does not substitute for a detector test. The detector requires the matching new firmware; building the project does not install that firmware.
4. Open the app and press **Start Logging** near the detector. Allow Bluetooth and location access. The first session connects to the first nearby matching MuonP4; subsequent sessions prefer the saved device. Stop logging and use **Use a different detector next time** to change it.
5. Wait for a connection. On **Detector**, sync time and set a run label. Press **Start Physics Run** when ready, or allow Wi-Fi's 120-second idle timeout.
6. Keep the app open for controls/downloads. Once monitoring is connected, the phone can be locked. Press **Stop Logging** to end the phone session and GPS updates; the detector keeps its own SD log.

## Live Activity and Dynamic Island

Starting logging in the foreground starts a Live Activity when permission is enabled. It presents the three main coincidence counts, detector temperature, pressure, the last minute's physics/setup label, and sample age. Compatible iPhones show compact, minimal and expanded Dynamic Island presentations; the Lock Screen has the full Live Activity view.

Illustrative text, not an actual Dynamic Island screenshot:

```text
MuonP4                         Physics run
CH01         CH02         CH12
 22           14           27       25.1 °C
                                  984.7 hPa
Last completed minute · Physics · Sample 10s ago
```

The compact island displays the combined CH01+CH02+CH12 count. Overdue data gets a warning rather than silently remaining “live.” **Restart Live Activity** creates a fresh activity from the foreground when needed. System time limits, force-quitting, range and iOS scheduling still apply. The app uses a service-specific Bluetooth connection and state restoration; it does not assume repeated background advertisement callbacks. [Apple's background-scanning clarification](https://developer.apple.com/forums/thread/815189).

## Correction example

Settings contain the defaults supplied from the clean external-battery analysis:

| Setting | Default |
|---|---:|
| Reference pressure P₀ | 982.864 hPa |
| Reference temperature T₀ | 24.637 °C |
| Pressure coefficient βP | −0.15 %/hPa |
| Combined temperature coefficient βT | −0.393 %/°C |
| CH01 temperature coefficient | −0.70 %/°C |
| CH02 temperature coefficient | −0.62 %/°C |
| CH12 temperature coefficient | +0.16 %/°C |

The model is:

```text
Ncorrected = Nmeasured × exp[−βP(P − P₀) − βT(T − T₀)]
```

The calculation uses fractional coefficients (`−0.0015` and `−0.00393` for the combined rate). For a **synthetic** 60-second physics interval containing 60 combined counts at 992.864 hPa and 25.637 °C:

```text
factor = exp[0.0015 × 10 + 0.00393 × 1] = 1.019110
corrected rate ≈ 61.1466 counts/min
raw rate = 60 counts/min
```

All coefficients/reference values are editable. The pressure coefficient was chosen on physical grounds. The temperature fits are statistically consistent with zero and remain provisional, not a fundamental detector calibration. No temperature coefficients were supplied for CH012 or auxiliary channels; those plots remain raw.

The chart normalizes by actual integration duration. Only physics-valid intervals with valid environmental readings are corrected. Changing settings recalculates the visible plot but does not rewrite earlier exported log entries; each new log entry stores the settings and corrected value used at that time.

The firmware also retains its separate five-minute hardware bias compensation (20 °C reference, DAC channels 0–3). That adjusts the detector bias; it is not the phone’s count-rate correction model. Actual DAC changes restart the minute integration so an interval straddling a bias change is not treated as stable physics.

## Reconnection and missed-minute example

Suppose cumulative CH01 counts rise from 1,000 to 1,120 and valid exposure rises by 120,000 ms, with the same device and boot ID. The app recovers **120 counts over two valid minutes**, or **60 counts/min** for that aggregate interval. It does not split the result into two invented minute records or apply a single recent temperature to both missing minutes.

A changed boot ID starts a new baseline. Setup minutes, HV-off periods, settling and discarded partial windows never enter cumulative physics totals. The phone retains up to 1,440 received records for its current process's chart; the exported JSONL retains the full logged session. SD remains the authoritative source for missing minute records.

## Detector tab: web-feature equivalents

| Web feature | Native iPhone control |
|---|---|
| Status, current/last/total counts | **Refresh detector status**, Live channels, Live tab |
| Browser clock sync and manual time | **Sync Phone Time**, manual date/time, **Set Manual Time** |
| Run label | Run label field and **Set Run Label** |
| Keep Wi-Fi on / auto-off | **Keep Wi-Fi On**, **Allow Wi-Fi Auto-Off** |
| Quiet run transition | **Start Physics Run** |
| HV byte and off | Hex byte field, **Set HV Byte**, **HV Off** |
| FPGA sequence | **HV Off → Program FPGA → Restore Startup HV** |
| Startup DAC and per-channel writes | **Set Startup DAC Values**, channel and 10-bit code |
| Environment status | Temperature, pressure, humidity and freshness |
| Current count/environment CSV | Corresponding download buttons |
| Recent RAM log CSV | **Download Recent Minute CSV** |
| Previous SD files | **Refresh Previous SD Files**, select a file, **Share Download** |

Bluetooth controls remain available after Wi-Fi is off. Turning Wi-Fi back on still requires a detector reboot. State-changing HV/FPGA/DAC controls present a confirmation in the app. Hardware operations are not automatically repeated after a timeout, because an accepted command may already have executed.

Downloads fix a byte limit for a growing file, verify sequential chunk offsets, and expose a completed export only after all bytes arrive. A cancelled/failed download is not presented as a complete file. Cancelling a wait cannot undo an already accepted hardware command.

## GPS, phone metadata and exports

GPS starts/stops with phone logging. Grant location permission; the app uses a visible background-location indicator and offers an Always-permission request in Settings. Denied or approximate location access does not stop detector logging.

Each exported phone record includes:

- Exact raw telemetry, device/boot identity, sequence, operating flags and cumulative exposure/counts.
- The correction settings and corrected combined rate, when eligible.
- Phone latitude/longitude, fix timestamp and age, horizontal accuracy, altitude/vertical accuracy, speed and course when valid, and reduced-accuracy status.
- Phone battery level/state, OS/model, timezone, thermal/low-power state and last discovery RSSI.

The GPS position belongs to the **phone**, not an independently located detector. The RSSI is a discovery measurement, not a continuous distance estimate. No detector battery-voltage sensor is implemented, so the app does not invent one.

An illustrative metadata excerpt (synthetic values; not a full exported record):

```json
{
  "protocolVersion": 4,
  "receivedAt": "2026-09-26T12:00:00Z",
  "correctedCoincidencesPerMinute": 61.1466,
  "phone": {
    "locationAgeSeconds": 2.0,
    "location": {
      "latitude": 40.0,
      "longitude": -75.0,
      "horizontalAccuracyM": 8.0,
      "altitudeM": 100.0,
      "reducedAccuracy": false
    }
  }
}
```

Use **Export phone log** for JSON Lines and **Share Download** for detector CSV files. Existing exports are available through Files → On My iPhone → MuonP4. Location remains in the phone log and is not broadcast or sent to the detector. Exported files include precise location when permission was granted.

## Bluetooth formats and compatibility

The primary advertisement includes the name and service UUID. Active-scan responses contain compact latest-minute CH01/CH02/CH12 counts, temperature, pressure and flags. Full connected data contains all seven minute counters and 64-bit cumulative physics totals. nRF scanners can show the name and raw custom bytes; they do not automatically render this experimental payload with labelled units.

Advertising windows occur roughly every 15 seconds even while a phone is connected; connected keepalives are additional radio activity. The existing S3-GEEK decoder is not compatible with this new format. See the [complete protocol and command API](bluetooth-protocol.md).

# Data Files And SD Logging

The firmware creates fresh files on the SD card for each detector start.

## Muon Count File

Before time sync:

```text
/sdcard/muon_unsynced_<uptime_seconds>.csv
```

After time sync:

```text
/sdcard/muon_YYYYMMDD_HHMMSS.csv
```

With a run label:

```text
/sdcard/muon_YYYYMMDD_HHMMSS_<label>.csv
```

If the filename already exists, a suffix such as `_01` is added.

## Muon CSV Columns

```csv
epoch,iso,ch01_p13,ch02_p12,ch12_p11,ch012_p22,gpio6_p31,gpio5_p29,gpio16_p36,sequence,uptime_ms,interval_ms,physics_valid,wifi_off,hv_settled,time_set,env_valid,temp_c,pressure_hpa
```

| Column | Meaning |
| --- | --- |
| `epoch` | Unix time in seconds since 1970-01-01 UTC |
| `iso` | readable timestamp |
| `ch01_p13` | CH0 and CH1 coincidence |
| `ch02_p12` | CH0 and CH2 coincidence |
| `ch12_p11` | CH1 and CH2 coincidence |
| `ch012_p22` | triple coincidence |
| `gpio6_p31` | raw channel output |
| `gpio5_p29` | raw channel output |
| `gpio16_p36` | raw channel output |

| `sequence` | Completed-record sequence for this boot |
| `uptime_ms` | Monotonic sample-end uptime |
| `interval_ms` | Actual integration duration; normalize rates using this |
| `physics_valid` | Complete stable physics interval, 0 or 1 |
| `wifi_off`, `hv_settled` | Operating-state flags at completion |
| `time_set` | Clock had been synchronized |
| `env_valid` | Fresh valid sensor values were available at completion |
| `temp_c`, `pressure_hpa` | Detector environmental snapshot; blank if invalid |

Completed setup minutes are retained with `physics_valid=0`. Physics totals include only completed valid intervals. HV/generation transitions discard the partial window and begin a new full integration. Normally a row spans about 60 seconds; delayed intervals over 61 seconds are not physics-valid. Select valid rows and divide counts by `interval_ms / 60000` for counts/minute.

No row is written while:

- HV is off
- HV is settling
- the detector is still in the startup settle delay

## Environment File

The environment file stores 5-minute BME280 averages:

```csv
epoch,iso,samples,temp_c_avg,pressure_hpa_avg,humidity_pct_avg
```

The BME280 is optional. If it is not present, muon counting still works.

## Downloading Data

From the web interface:

- **Download Current File** gets the active muon file.
- **Download Environment File** gets the active environment file.
- **Previous SD Files** lists old `.csv` and `.log` files newest first.

The newest live minute row is also available at:

```text
http://192.168.4.1/api/latest.txt
```

## iPhone export

The phone exports JSON Lines containing raw full telemetry, cumulative count differences, exposure, correction settings, corrected combined rate, GPS metadata and phone state. Values from setup or invalid sensors are not silently corrected. See [examples](iphone-guide.md#correction-example) and the [wire format](bluetooth-protocol.md). Existing CSV readers should accept the appended columns rather than assume exactly nine columns.

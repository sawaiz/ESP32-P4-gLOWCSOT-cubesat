# Troubleshooting

## Wi-Fi Network Not Visible

Expected network:

```text
SSID: MuonReadout
Password: glowcost
```

The AP automatically stops after 120 continuous seconds without a Wi-Fi client. A Bluetooth connection does not hold it open. Reset or power-cycle the ESP32-P4 to restart the setup window.

If you want more time on the web UI, connect quickly and press **Keep Wi-Fi On**.

## SD Card Not Ready

Check:

- SD card is inserted.
- Card is formatted FAT-compatible.
- Card contacts are clean.
- Board is power-cycled after inserting the card.

The web UI highlights SD card readiness at the top of the page.

## BME280 Not Found

Check:

- BME280 power is 3.3 V.
- GND is common with ESP32-P4/readout board.
- SDA is on Pi physical pin 3 / ESP32-P4 GPIO7.
- SCL is on Pi physical pin 5 / ESP32-P4 GPIO8.
- Address is `0x76` or `0x77`.

If the BME wiring holds SCL or SDA low, the DACx578 may also fail because it is on the same I2C bus. The firmware will keep HV off if DAC startup fails.

## DAC Startup Fails

If serial log shows DAC timeout or startup HV remains off:

- Check I2C wiring.
- Check DACx578 address `0x47`.
- Check BME280 wiring, because a bad BME connection can hold the whole I2C bus low.
- Reset after fixing wiring.

The firmware intentionally leaves HV off if DAC startup fails.

## FPGA Not Programmed

Healthy boot should include FPGA DONE high and FPGA program OK.

If FPGA programming fails:

- Check SPI pins.
- Check FPGA reset and DONE wiring.
- Confirm `main/fpga.bin` is present.
- Use the web FPGA reflash button, which turns HV off before programming.

## Counts Are Extremely High

Possible causes:

- Threshold too low.
- Wi-Fi still on and coupling noise.
- HV startup noise was included because data was inspected before settle.
- SiPM/scintillator disconnected or noisy.
- FPGA bitstream mismatch.

Threshold observations:

- `0x040` was too noisy in the test session.
- `0x080` was quiet.
- `0x070` is the current compromise.

## Counts Are Zero

Check:

- HV is enabled.
- HV settle delay has completed.
- FPGA DONE is true.
- Scintillators/SiPMs are connected.
- Threshold is not too high.
- The detector is not in HV-off state after FPGA flashing.

## No Serial Minute Rows After Power Save

Minute CSV records are written to SD rather than continuously streamed on the console. The `counts` command prints a non-destructive live snapshot; it does not reset the integration window.

Use the web UI before Wi-Fi turns off, or read the CSV file from the SD card after the run.

## iPhone cannot connect or updates stop

- Check that the detector is running protocol-v4 firmware; an older image may have no BLE service or an incompatible payload.
- Advertising is sparse: keep the app open near the detector until connected. The primary service UUID is `73B47A10-6F6E-4D75-9A50-4D756F6E5034`.
- One phone connection is supported. Stop logging on another connected phone first.
- Full controls require encrypted pairing; accept the pairing prompt when connecting. The peripheral explicitly requests pairing and exchanges bonding keys.
- After force-quitting, reopen and start logging again. A Live Activity does not remove all iOS background restrictions.
- Overdue readings are stale, not zero counts. Reconnection can recover cumulative totals within the same boot, but not missed minute-by-minute environmental data.
- The C6 firmware must support hosted BLE as well as Wi-Fi. A successful P4 build alone cannot verify the installed C6 image.

## Corrected chart is missing or almost matches raw counts

Only valid physics records with valid environment data are corrected. CH012 and auxiliary channels have no supplied temperature coefficients and show raw data. Corrections near the reference conditions are small, so traces can overlap. Settings use percent per unit; calculations use fractional coefficients. Temperature fits remain provisional.

## GPS or Dynamic Island is unavailable

Allow location and Bluetooth access, start logging in the foreground, and wait for the connection before locking the phone. GPS age/accuracy is recorded even when a fix is stale or approximate. Check Live Activity permissions and use **Restart Live Activity** from the app if it has ended. Dynamic Island requires compatible hardware; other supported iPhones use the Lock Screen Live Activity.

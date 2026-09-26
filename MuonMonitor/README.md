# MuonP4 iPhone app

Open **MuonMonitor.xcodeproj** and build the **MuonMonitor** scheme. iOS 17+, SwiftUI, CoreBluetooth, ActivityKit, Charts and CoreLocation; no external packages or server required.

Select your development team for both the app and MuonLive extension before running on a physical iPhone. The firmware service and matching wire protocol must be installed on the detector first through a separately authorized operation.

Features: Live Activity / Dynamic Island, all seven count channels, cumulative physics counts, editable pressure/temperature corrections, raw/corrected charts, GPS and phone metadata, local JSONL export, full Bluetooth detector controls and SD downloads.

The full operating guide and limits are in `../docs/iphone-guide.md`; wire format is in `../docs/bluetooth-protocol.md`. Default correction coefficients came from the user's clean external-battery analysis and are deliberately identified as provisional.

A Debug-only `--demo` launch argument shows clearly marked synthetic readings without starting Bluetooth or GPS. Demo data is never used by the normal launch path.

To compile without signing:

```sh
xcodebuild -project MuonMonitor.xcodeproj -scheme MuonMonitor -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO build
```

The project does not require cloud access. Only the user-triggered share sheet exports data out of the app's local files.

import Foundation
import CoreLocation
import UIKit
import Combine

struct LocationRecord: Codable {
    let timestamp:Date
    let latitude:Double
    let longitude:Double
    let altitudeM:Double?
    let horizontalAccuracyM:Double
    let verticalAccuracyM:Double?
    let speedMS:Double?
    let courseDegrees:Double?
    let reducedAccuracy:Bool
}
struct PhoneRecord: Codable {
    let location:LocationRecord?
    let locationAgeSeconds:Double?
    let locationPermission:String
    let batteryFraction:Float?
    let batteryState:Int
    let lowPowerMode:Bool
    let thermalState:Int
    let osVersion:String
    let model:String
    let timezone:String
    let appVersion:String
    let bluetoothRSSI:Int?
}
final class PhoneMetadata:NSObject,ObservableObject,CLLocationManagerDelegate {
    private let manager=CLLocationManager()
    private var running=false
    @Published var location:LocationRecord?
    @Published var permission="Location not requested"
    override init() {
        super.init();manager.delegate=self
        manager.desiredAccuracy=kCLLocationAccuracyBest
        manager.distanceFilter=10
        manager.activityType = .other
        manager.pausesLocationUpdatesAutomatically=false
        manager.showsBackgroundLocationIndicator=true
        UIDevice.current.isBatteryMonitoringEnabled=true
    }
    func start() {
        running=true
        if manager.authorizationStatus == .notDetermined { manager.requestWhenInUseAuthorization() }
        else { resumeIfAllowed() }
    }
    func enableAlways() { manager.requestAlwaysAuthorization() }
    func stop() { running=false;manager.stopUpdatingLocation();manager.allowsBackgroundLocationUpdates=false }
    private func resumeIfAllowed() {
        guard running else { return }
        switch manager.authorizationStatus {
        case .authorizedAlways,.authorizedWhenInUse:
            manager.allowsBackgroundLocationUpdates=true
            manager.startUpdatingLocation()
        default: break
        }
    }
    func locationManagerDidChangeAuthorization(_ manager:CLLocationManager) {
        switch manager.authorizationStatus {
        case .authorizedAlways: permission="Always allowed"
        case .authorizedWhenInUse: permission="While in use; active background session"
        case .denied,.restricted: permission="Location unavailable; logging without GPS"
        default: permission="Location not requested"
        }
        resumeIfAllowed()
    }
    func locationManager(_ manager:CLLocationManager,didUpdateLocations locations:[CLLocation]) {
        guard let l=locations.last,l.horizontalAccuracy>=0 else { return }
        location=LocationRecord(timestamp:l.timestamp,latitude:l.coordinate.latitude,longitude:l.coordinate.longitude,
            altitudeM:l.verticalAccuracy>=0 ? l.altitude:nil,horizontalAccuracyM:l.horizontalAccuracy,
            verticalAccuracyM:l.verticalAccuracy>=0 ? l.verticalAccuracy:nil,speedMS:l.speed>=0 ? l.speed:nil,
            courseDegrees:l.course>=0 ? l.course:nil,reducedAccuracy:manager.accuracyAuthorization == .reducedAccuracy)
    }
    func locationManager(_ manager:CLLocationManager,didFailWithError error:Error) { permission="GPS update unavailable: \(error.localizedDescription)" }
    func snapshot(rssi:Int?) -> PhoneRecord {
        let d=UIDevice.current,p=ProcessInfo.processInfo
        return PhoneRecord(location:location,locationAgeSeconds:location.map { max(0,Date().timeIntervalSince($0.timestamp)) },
            locationPermission:permission,batteryFraction:d.batteryLevel>=0 ? d.batteryLevel:nil,batteryState:d.batteryState.rawValue,
            lowPowerMode:p.isLowPowerModeEnabled,thermalState:p.thermalState.rawValue,osVersion:d.systemVersion,model:d.model,
            timezone:TimeZone.current.identifier,appVersion:Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "unknown",bluetoothRSSI:rssi)
    }
}

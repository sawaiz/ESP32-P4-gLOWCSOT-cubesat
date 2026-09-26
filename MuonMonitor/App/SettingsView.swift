import SwiftUI

struct SettingsView:View {
    @ObservedObject var monitor:Monitor
    var body:some View {
        NavigationStack {
            Form {
                Section("Corrections") {
                    Toggle("Show corrected rates",isOn:$monitor.correction.enabled)
                    coefficient("Reference pressure (hPa)",$monitor.correction.referencePressure)
                    coefficient("Reference temperature (°C)",$monitor.correction.referenceTemperature)
                    coefficient("Pressure coefficient (%/hPa)",Binding(get:{monitor.correction.pressureBeta*100},set:{monitor.correction.pressureBeta=$0/100}))
                    coefficient("Combined temperature (%/°C)",Binding(get:{monitor.correction.totalTemperatureBeta*100},set:{monitor.correction.totalTemperatureBeta=$0/100}))
                    ForEach(0..<3) { i in
                        coefficient("\(Telemetry.channels[i]) temperature (%/°C)",Binding(get:{monitor.correction.pairTemperatureBetas[i]*100},set:{monitor.correction.pairTemperatureBetas[i]=$0/100}))
                    }
                    Text("Ncorr = Nraw × exp[−βP(P−P₀) − βT(T−T₀)]\nSettings use percent units; the calculation divides them by 100.").font(.caption).foregroundStyle(.secondary)
                    Text("The pressure coefficient was chosen on physical grounds. Temperature coefficients were fitted to the clean external-battery run and are statistically consistent with zero; they are provisional.").font(.caption).foregroundStyle(.orange)
                    Button("Restore supplied defaults") { monitor.correction=Correction() }
                }
                Section("Location and phone metadata") {
                    LocationDetails(phone:monitor.phone)
                    Text("GPS stays in the phone log. It is never sent in detector advertisements. Position is tagged with its actual timestamp and accuracy, so a stale fix cannot look current.").font(.caption).foregroundStyle(.secondary)
                }
                Section("Background operation") {
                    Text("Start Logging while the app is open, wait for a connection, then lock the phone. Bluetooth uses a service-specific connection and restores it when iOS permits. Force-quitting the app stops background monitoring until reopened.")
                    Text("Dynamic Island requires a compatible iPhone. Live Activities have system time limits; reopen the app and start a new activity when needed. The detector continues logging independently.")
                }
            }.navigationTitle("Settings")
        }
    }
    private func coefficient(_ title:String,_ value:Binding<Double>) -> some View {
        VStack(alignment:.leading) {
            Text(title).font(.caption).foregroundStyle(.secondary)
            TextField(title,value:value,format:.number.precision(.fractionLength(0...6))).keyboardType(.numbersAndPunctuation).monospacedDigit()
        }
    }
}
struct LocationDetails:View {
    @ObservedObject var phone:PhoneMetadata
    var body:some View {
        Text(phone.permission)
        if let l=phone.location {
            Text(String(format:"%.6f, %.6f",l.latitude,l.longitude)).monospaced()
            Text(String(format:"Accuracy ±%.1f m",l.horizontalAccuracyM))
            if let altitude=l.altitudeM { Text(String(format:"Altitude %.1f m",altitude)) }
            Text("Fix: \(l.timestamp.formatted())").font(.caption)
            if l.reducedAccuracy { Text("Approximate location permission").foregroundStyle(.orange) }
        }
        Button("Allow location access across background sessions") { phone.enableAlways() }
    }
}

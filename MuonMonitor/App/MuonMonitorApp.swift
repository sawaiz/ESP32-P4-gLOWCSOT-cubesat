import SwiftUI

@main struct MuonMonitorApp: App {
    @StateObject private var monitor=Monitor()
    @State private var selectedTab=ProcessInfo.processInfo.arguments.contains("--demo-charts") ? 1 : 0
    var body: some Scene { WindowGroup {
        TabView(selection:$selectedTab) {
            Dashboard(monitor:monitor).tag(0).tabItem { Label("Live",systemImage:"waveform.path") }
            ChartsView(monitor:monitor).tag(1).tabItem { Label("Charts",systemImage:"chart.xyaxis.line") }
            ControlsView(monitor:monitor).tag(2).tabItem { Label("Detector",systemImage:"slider.horizontal.3") }
            SettingsView(monitor:monitor).tag(3).tabItem { Label("Settings",systemImage:"gearshape") }
        }.tint(.cyan).preferredColorScheme(.dark)
    } }
}
struct Dashboard: View {
    @ObservedObject var monitor:Monitor
    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment:.leading,spacing:22) {
                    VStack(alignment:.leading,spacing:8) {
                        Label("MUON OBSERVATORY",systemImage:"sparkles").font(.caption.weight(.semibold)).foregroundStyle(.cyan)
                        Text("Your detector, live.").font(.largeTitle.bold())
                        Text(monitor.status).foregroundStyle(.secondary)
                        if let date=monitor.lastSeen {
                            TimelineView(.periodic(from:.now,by:1)) { context in
                                let stale=context.date.timeIntervalSince(date)>90
                                Text(stale ? "Update overdue · last seen \(date.formatted(date:.omitted,time:.standard))" : "Connected · last seen \(date.formatted(date:.omitted,time:.standard))")
                                    .font(.caption).foregroundStyle(stale ? .orange : .secondary)
                            }
                        }
                    }
                    Button(action:{ monitor.logging ? monitor.stop() : monitor.start() }) {
                        Label(monitor.logging ? "Stop Logging" : "Start Logging",systemImage:monitor.logging ? "stop.fill" : "play.fill")
                            .frame(maxWidth:.infinity).padding(10)
                    }.disabled(monitor.demo).buttonStyle(.borderedProminent).tint(.cyan).foregroundStyle(.black)
                    if monitor.logging { Button("Restart Live Activity") { monitor.restartLiveActivity() }.font(.caption) }
                    if let issue=monitor.issue { Text(issue).font(.callout).foregroundStyle(.orange) }
                    if let s=monitor.sample {
                        VStack(alignment:.leading,spacing:12) {
                            Text("LAST COMPLETED MINUTE").font(.caption).foregroundStyle(.secondary)
                            Text(s.hasSample ? "\(s.coincidenceCount)" : "—").font(.system(size:60,weight:.light,design:.rounded))
                            Text(s.hasSample ? (s.physics ? "Physics · CH01 + CH02 + CH12" : "Setup data · excluded from physics totals") : "Waiting for the first complete minute").font(.caption).foregroundStyle(s.physics ? .cyan : .orange)
                            if s.hasSample { Text("Sample ended at uptime \(s.sampleUptime / 1000)s · interval \(Double(s.interval)/1000,specifier:"%.1f")s").font(.caption2).foregroundStyle(.secondary) }
                            Divider()
                            ForEach(0..<7) { i in
                                HStack { Text(Telemetry.channels[i]); Spacer(); Text(s.hasSample ? "\(s.counts[i])" : "—").monospacedDigit(); Text("Σ \(s.totals[i])").monospacedDigit().foregroundStyle(.secondary).frame(width:110,alignment:.trailing) }
                            }.font(.callout)
                            Text("Σ = cumulative counts from completed valid physics intervals this boot.").font(.caption2).foregroundStyle(.secondary)
                        }.padding().background(.white.opacity(0.055),in:RoundedRectangle(cornerRadius:20))
                        HStack {
                            metric("TEMPERATURE",s.temperature.map { String(format:"%.2f °C",$0) } ?? "Unavailable")
                            metric("PRESSURE",s.pressure.map { String(format:"%.2f hPa",$0) } ?? "Unavailable")
                        }
                        if let d=monitor.delta {
                            Text(String(format:"Recovered interval: %.1f coincidences/min across %.1f minutes of valid exposure.",d.coincidencesPerMinute,Double(d.exposureMS)/60000)).font(.caption).foregroundStyle(.secondary)
                        }
                        Text("\(s.deviceID) · Sequence \(s.sequence)\nFPGA \(s.status & 1 != 0 ? "ready" : "not ready") · SD \(s.status & 2 != 0 ? "mounted" : "unavailable") · HV 0x\(String(format:"%02X",s.hv))")
                            .font(.caption.monospaced()).foregroundStyle(.secondary)
                    } else {
                        Text("Start logging near MuonP4. The app connects to your saved detector, or the first nearby MuonP4 on your first run. Keep the app open until connected, then lock your phone.")
                            .foregroundStyle(.secondary)
                    }
                    if let url=monitor.logURL { ShareLink(item:url) { Label("Export phone log",systemImage:"square.and.arrow.up") } }
                    Text("The Live Activity appears on the Lock Screen and, on supported iPhones, in the Dynamic Island. Updates can pause when out of range or when iOS suspends access. The detector’s SD card remains the full minute-by-minute record.")
                        .font(.caption).foregroundStyle(.secondary)
                    Button("Use a different detector next time") { monitor.forgetDetector() }.font(.caption).disabled(monitor.logging)
                }.padding(22)
            }.background(Color(red:0.035,green:0.055,blue:0.08)).navigationTitle("MuonP4").navigationBarTitleDisplayMode(.inline)
        }
    }
    private func metric(_ title:String,_ value:String) -> some View {
        VStack(alignment:.leading,spacing:8) { Text(title).font(.caption2).foregroundStyle(.secondary); Text(value).font(.headline.monospacedDigit()) }
            .frame(maxWidth:.infinity,alignment:.leading).padding().background(.white.opacity(0.055),in:RoundedRectangle(cornerRadius:16))
    }
}

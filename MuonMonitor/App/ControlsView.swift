import SwiftUI

struct DetectorFile:Identifiable { var id:String { name };let name:String,size:Int,modified:Double }
struct ControlsView:View {
    @ObservedObject var monitor:Monitor
    @State private var busy=false
    @State private var message=""
    @State private var label=""
    @State private var hv="ea"
    @State private var dacValue="02f1"
    @State private var dacChannel=0
    @State private var manualDate=Date()
    @State private var details:[String:Any]=[:]
    @State private var environment:[String:Any]=[:]
    @State private var live:[Int]=[]
    @State private var last:[Int]=[]
    @State private var totals:[String]=[]
    @State private var files:[DetectorFile]=[]
    @State private var export:URL?
    @State private var confirm:String?
    @State private var transfer:Task<Void,Never>?
    var body:some View {
        NavigationStack {
            Form {
                Section {
                    Text(monitor.controlsReady ? "Bluetooth controls ready" : "Start Logging and connect first")
                    if busy { ProgressView(message) } else if !message.isEmpty { Text(message).font(.caption) }
                    Button("Refresh detector status") { run { try await refresh() } }
                }
                Section("Time and SD run label") {
                    Button("Sync Phone Time") { send("time",["epoch":Int(Date().timeIntervalSince1970)]) }
                    DatePicker("Manual time",selection:$manualDate)
                    Button("Set Manual Time") { send("time",["epoch":Int(manualDate.timeIntervalSince1970)]) }
                    TextField("Run label",text:$label).textInputAutocapitalization(.never)
                    Button("Set Run Label") { send("label",["label":String(label.prefix(32))]) }
                    if let value=details["label"] as? String { LabeledContent("Current label",value:value) }
                }
                Section("Power") {
                    Button("Start Physics Run") { confirm="physics" }
                    Button("Keep Wi-Fi On") { send("wifi_keep",["enable":1]) }
                    Button("Allow Wi-Fi Auto-Off") { send("wifi_keep",["enable":0]) }
                    Text("Wi-Fi turns off after 120 seconds without a Wi-Fi client. Bluetooth connections do not hold Wi-Fi open. After shutdown Wi-Fi stays off until a device reboot.").font(.caption).foregroundStyle(.secondary)
                }
                Section("High voltage") {
                    TextField("HV byte (hex)",text:$hv).textInputAutocapitalization(.never).autocorrectionDisabled()
                    Button("Set HV Byte") { confirm="hv" }
                    Button("HV Off") { send("hv",["value":0]) }
                    Text("Changing HV interrupts counting and starts a new settling interval.").font(.caption).foregroundStyle(.secondary)
                }
                Section("FPGA and DAC") {
                    Button("HV Off → Program FPGA → Restore Startup HV") { confirm="fpga" }
                    Button("Set Startup DAC Values") { confirm="dac_startup" }
                    Picker("DAC channel",selection:$dacChannel) { ForEach(0..<8) { Text("\($0)").tag($0) } }
                    TextField("10-bit DAC code (hex 000–3ff)",text:$dacValue).textInputAutocapitalization(.never).autocorrectionDisabled()
                    Button("Set DAC Channel") { confirm="dac" }
                    if let codes=details["dac"] as? [Int] { Text(codes.map { String(format:"%03x",$0) }.joined(separator:" · ")).font(.caption.monospaced()) }
                }
                Section("Live channels · current / last / total") {
                    ForEach(0..<live.count,id:\.self) { i in
                        LabeledContent(Telemetry.channels[i],value:"\(live[i]) / \(last.indices.contains(i) ? String(last[i]) : "—") / \(totals.indices.contains(i) ? totals[i] : "—")")
                    }
                    Text("These web-compatible totals include setup. Physics-only cumulative totals appear on the Live tab.").font(.caption).foregroundStyle(.secondary)
                }
                Section("Environment") {
                    if let t=environment["temp_c"] as? Double { LabeledContent("Temperature",value:String(format:"%.2f °C",t)) }
                    if let p=environment["pressure_hpa"] as? Double { LabeledContent("Pressure",value:String(format:"%.2f hPa",p)) }
                    if let h=environment["humidity_pct"] as? Double { LabeledContent("Humidity",value:String(format:"%.2f %%",h)) }
                    LabeledContent("Valid fresh reading",value:environment["valid"] as? Bool == true ? "Yes":"No")
                }
                Section("Logs and downloads") {
                    Button("Download Current Count File") { download(details["log"] as? String) }
                    Button("Download Environment File") { download(details["env"] as? String) }
                    Button("Download Recent Minute CSV") { run { export=try await monitor.downloadRAM();message="Minute log downloaded" } }
                    Button("Refresh Previous SD Files") { run { try await loadFiles() } }
                    ForEach(files) { file in
                        Button { download(file.name) } label: {
                            VStack(alignment:.leading) { Text(file.name).font(.caption.monospaced());Text("\(file.size) bytes · \(Date(timeIntervalSince1970:file.modified).formatted())").font(.caption2).foregroundStyle(.secondary) }
                        }
                    }
                }
            }
            .disabled(busy || !monitor.controlsReady)
            .safeAreaInset(edge:.bottom) {
                VStack {
                    if busy { Button("Cancel waiting / download") { transfer?.cancel();monitor.cancelOperation() }.padding(8);Text("A hardware command already accepted by the detector can still finish.").font(.caption2) }
                    if let export { ShareLink(item:export) { Label("Share Download",systemImage:"square.and.arrow.up") }.padding(12) }
                }.frame(maxWidth:.infinity).background(.ultraThinMaterial)
            }
            .navigationTitle("Detector")
            .confirmationDialog("Change detector state?",isPresented:Binding(get:{confirm != nil},set:{if !$0 { confirm=nil }}),titleVisibility:.visible) {
                Button("Apply Change",role:.destructive) { applyConfirmation() }
            } message: { Text("This can interrupt a physics interval. Setup and transitions are excluded from valid physics records.") }
            .onAppear { if monitor.controlsReady && details.isEmpty { run { try await refresh() } } }
        }
    }
    private func run(_ work:@escaping () async throws -> Void) {
        guard !busy else { return };busy=true;message="Working…"
        transfer=Task { do { try await work();if message=="Working…" { message="Done" } } catch { message=error.localizedDescription };busy=false }
    }
    private func send(_ op:String,_ args:[String:Any]=[:]) { run { _=try await monitor.command(op,args);try await refresh();message="Done" } }
    private func refresh() async throws {
        details=try await monitor.command("status")
        environment=try await monitor.command("environment")
        let counts=try await monitor.command("counts")
        live=counts["live"] as? [Int] ?? [];last=counts["last"] as? [Int] ?? [];totals=counts["total"] as? [String] ?? []
    }
    private func loadFiles() async throws {
        files=[]
        for index in 0..<100000 {
            try Task.checkCancellation()
            let r=try await monitor.command("files",["index":index])
            if r["eof"] as? Bool == true { break }
            if let name=r["name"] as? String { files.append(DetectorFile(name:name,size:r["size"] as? Int ?? 0,modified:r["modified"] as? Double ?? 0)) }
        }
        files.sort { $0.modified>$1.modified }
    }
    private func download(_ name:String?) {
        guard let name,!name.isEmpty else { message="Refresh status first; no file selected";return }
        run { export=try await monitor.download(name:name) { n,total in message="Downloading \(n) / \(total) bytes" };message="Download complete" }
    }
    private func applyConfirmation() {
        let operation=confirm;confirm=nil
        switch operation {
        case "physics":send("start_physics")
        case "fpga":send("fpga")
        case "dac_startup":send("dac_startup")
        case "hv":if let value=UInt8(hv,radix:16) { send("hv",["value":Int(value)]) } else { message="Enter a byte from 00 to ff" }
        case "dac":if let value=UInt16(dacValue,radix:16),value<=1023 { send("dac",["channel":dacChannel,"value":Int(value)]) } else { message="Enter a 10-bit code from 000 to 3ff" }
        default:break
        }
    }
}

import Foundation
import Combine
import CoreBluetooth
import ActivityKit

final class Monitor: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    static let service = CBUUID(string:"73B47A10-6F6E-4D75-9A50-4D756F6E5034")
    static let sampleID = CBUUID(string:"73B47A11-6F6E-4D75-9A50-4D756F6E5034")
    static let controlID=CBUUID(string:"73B47A12-6F6E-4D75-9A50-4D756F6E5034")
    static let responseID=CBUUID(string:"73B47A13-6F6E-4D75-9A50-4D756F6E5034")
    let phone=PhoneMetadata()
    @Published var demo=false
    @Published var history:[ChartSample]=[]
    @Published var correction=Correction() { didSet { if let d=try? JSONEncoder().encode(correction) { UserDefaults.standard.set(d,forKey:"correction") } } }
    @Published var controlsReady=false
    @Published var logging = false
    @Published var status = "Ready"
    @Published var sample: Telemetry?
    @Published var lastSeen: Date?
    @Published var delta: PhysicsDelta?
    @Published var issue: String?
    @Published var logURL: URL?
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var characteristic: CBCharacteristic?
    private var activity: Activity<MuonActivity>?
    private var previous: Telemetry?
    private var lastLogged: Telemetry?
    private var commandCharacteristic:CBCharacteristic?
    private var responseCharacteristic:CBCharacteristic?
    private var controlContinuation:CheckedContinuation<[String:Any],Error>?
    private var requestID:UInt32=UInt32.random(in:1...UInt32.max)
    private var controlTimeout:DispatchWorkItem?
    private var rssi:Int?
    private var readPending = false
    private let defaults = UserDefaults.standard

    override init() {
        super.init()
        #if DEBUG
        if ProcessInfo.processInfo.arguments.contains("--demo") { loadDemo();return }
        #endif
        if let data=defaults.data(forKey:"correction"),let c=try? JSONDecoder().decode(Correction.self,from:data) { correction=c }
        logging=defaults.bool(forKey:"logging")
        if let saved=defaults.data(forKey:"previous") {
            previous=try? JSONDecoder().decode(Telemetry.self,from:saved)
            lastLogged=previous
        }
        if let path=defaults.string(forKey:"logPath"), FileManager.default.fileExists(atPath:path) { logURL=URL(fileURLWithPath:path) }
        activity=Activity<MuonActivity>.activities.first
        if logging { phone.start() }
        central=CBCentralManager(delegate:self,queue:.main,options:[CBCentralManagerOptionRestoreIdentifierKey:"MuonP4.monitor"])
    }
    func start() {
        guard !logging else { return }
        issue=nil; logging=true; defaults.set(true,forKey:"logging")
        previous=nil; lastLogged=nil; defaults.removeObject(forKey:"previous")
        history=[]; phone.start(); createLog()
        if ActivityAuthorizationInfo().areActivitiesEnabled {
            do {
                activity=try Activity.request(attributes:MuonActivity(),content:ActivityContent(state:.waiting,staleDate:Date().addingTimeInterval(90)),pushType:nil)
            } catch { issue="Live Activity could not start: \(error.localizedDescription)" }
        } else { issue="Live Activities are disabled for this app. Logging can still run." }
        discover()
    }
    func stop() {
        phone.stop(); finishControl(.failure(ControlError(message:"Logging stopped")))
        logging=false; defaults.set(false,forKey:"logging"); central.stopScan()
        if let p=peripheral { central.cancelPeripheralConnection(p) }
        peripheral=nil; characteristic=nil; readPending=false
        status="Logging stopped"
        if let a=activity { Task { await a.end(nil,dismissalPolicy:.immediate) } }
        activity=nil
    }
    func forgetDetector() {
        guard !logging else { return }
        defaults.removeObject(forKey:"peripheral")
        status="Next run will find a nearby MuonP4"
    }
    private func createLog() {
        let dir=FileManager.default.urls(for:.documentDirectory,in:.userDomainMask)[0]
        let url=dir.appendingPathComponent("MuonP4-\(Int(Date().timeIntervalSince1970)).jsonl")
        do {
            try Data().write(to:url,options:.atomic)
            try FileManager.default.setAttributes([.protectionKey:FileProtectionType.completeUntilFirstUserAuthentication],ofItemAtPath:url.path)
            logURL=url; defaults.set(url.path,forKey:"logPath")
        } catch { issue="Cannot create phone log: \(error.localizedDescription)"; logURL=nil }
    }
    private func discover() {
        guard logging, central.state == .poweredOn else { return }
        if let p=peripheral {
            if p.state == .connected { p.discoverServices([Self.service]); return }
            if p.state == .connecting { return }
        }
        if let raw=defaults.string(forKey:"peripheral"), let id=UUID(uuidString:raw),
           let p=central.retrievePeripherals(withIdentifiers:[id]).first {
            connect(p); return
        }
        status="Looking for MuonP4"
        central.scanForPeripherals(withServices:[Self.service],options:[CBCentralManagerScanOptionAllowDuplicatesKey:false])
        publishActivity()
    }
    private func connect(_ p: CBPeripheral) {
        central.stopScan(); peripheral=p; p.delegate=self
        status="Connecting to MuonP4"
        central.connect(p,options:nil); publishActivity()
    }
    func centralManagerDidUpdateState(_ central:CBCentralManager) {
        switch central.state {
        case .poweredOn: discover()
        case .poweredOff: status="Bluetooth is off"
        case .unauthorized: status="Allow Bluetooth access in Settings"
        case .unsupported: status="Bluetooth is unavailable"
        default: status="Waiting for Bluetooth"
        }
        publishActivity()
    }
    func centralManager(_ central:CBCentralManager,willRestoreState dict:[String:Any]) {
        guard logging, let items=dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],let p=items.first else { return }
        peripheral=p; p.delegate=self
        if p.state == .connected { p.discoverServices([Self.service]) }
    }
    func centralManager(_ central:CBCentralManager,didDiscover p:CBPeripheral,advertisementData:[String:Any],rssi RSSI:NSNumber) {
        guard logging,peripheral == nil else { return }
        rssi=RSSI.intValue;connect(p)
    }
    func centralManager(_ central:CBCentralManager,didConnect p:CBPeripheral) {
        defaults.set(p.identifier.uuidString,forKey:"peripheral")
        p.delegate=self; readPending=false; status="Reading detector"
        p.discoverServices([Self.service]); publishActivity()
    }
    func centralManager(_ central:CBCentralManager,didFailToConnect p:CBPeripheral,error:Error?) {
        characteristic=nil; readPending=false; controlsReady=false; commandCharacteristic=nil; responseCharacteristic=nil
        finishControl(.failure(ControlError(message:"Bluetooth connection lost")))
        status="Waiting to reconnect"; publishActivity()
        if logging { central.connect(p,options:nil) }
    }
    func centralManager(_ central:CBCentralManager,didDisconnectPeripheral p:CBPeripheral,error:Error?) {
        characteristic=nil; readPending=false; controlsReady=false; commandCharacteristic=nil; responseCharacteristic=nil
        finishControl(.failure(ControlError(message:"Bluetooth connection lost")))
        status=logging ? "Detector out of range" : "Logging stopped"; publishActivity()
        if logging { central.connect(p,options:nil) }
    }
    func peripheral(_ p:CBPeripheral,didDiscoverServices error:Error?) {
        guard error == nil,let service=p.services?.first(where:{$0.uuid == Self.service}) else {
            issue="Detector service unavailable. This app needs the updated firmware."; return
        }
        p.discoverCharacteristics([Self.sampleID,Self.controlID,Self.responseID],for:service)
    }
    func peripheral(_ p:CBPeripheral,didDiscoverCharacteristicsFor service:CBService,error:Error?) {
        guard error == nil,let c=service.characteristics?.first(where:{$0.uuid == Self.sampleID}) else {
            issue="Telemetry characteristic unavailable"; return
        }
        characteristic=c; p.setNotifyValue(true,for:c); read(p,c)
        commandCharacteristic=service.characteristics?.first { $0.uuid==Self.controlID }
        responseCharacteristic=service.characteristics?.first { $0.uuid==Self.responseID }
        if let responseCharacteristic { p.setNotifyValue(true,for:responseCharacteristic) }
        controlsReady=commandCharacteristic != nil && responseCharacteristic != nil
    }
    func peripheral(_ p:CBPeripheral,didUpdateNotificationStateFor c:CBCharacteristic,error:Error?) {
        if let error { issue="Background updates unavailable: \(error.localizedDescription)" }
    }
    private func read(_ p:CBPeripheral,_ c:CBCharacteristic) {
        guard !readPending else { return }
        readPending=true; p.readValue(for:c)
    }
    func peripheral(_ p:CBPeripheral,didUpdateValueFor c:CBCharacteristic,error:Error?) {
        if c.uuid==Self.responseID { receiveControl(p,c,error); return }
        guard logging,c.uuid == Self.sampleID else { return }
        if let error { readPending=false; issue="Read failed: \(error.localizedDescription)"; return }
        guard let data=c.value else { readPending=false; return }
        if data.count == 8, data[0] == 77, data[1] == 78, data[2] == 4 { read(p,c); return }
        readPending=false
        guard let value=Telemetry(data:data) else { issue="Unsupported or incomplete telemetry"; return }
        lastSeen=Date(); sample=value
        status=value.transition ? "HV settling" : value.physicsReady ? "Physics run" : "Setup / HV off"
        if value.hasSample && (lastLogged?.bootID != value.bootID || lastLogged?.sequence != value.sequence || lastLogged?.deviceID != value.deviceID) {
            history.append(ChartSample(received:Date(),sample:value))
            if history.count>1440 { history.removeFirst(history.count-1440) }
            delta=previous.flatMap { value.delta(from:$0) }
            if append(value) { lastLogged=value }
            previous=value
            if let encoded=try? JSONEncoder().encode(value) { defaults.set(encoded,forKey:"previous") }
        }
        publishActivity()
    }
    private struct Record: Encodable {
        let protocolVersion=4
        let receivedAt:Date
        let sample:Telemetry
        let recoveredPhysicsDelta:PhysicsDelta?
        let phone:PhoneRecord
        let correction:Correction
        let correctedCoincidencesPerMinute:Double?
    }
    @discardableResult private func append(_ value:Telemetry) -> Bool {
        if logURL == nil { createLog() }
        guard let url=logURL else { return false }
        do {
            let encoder=JSONEncoder(); encoder.dateEncodingStrategy = .iso8601
            var data=try encoder.encode(Record(receivedAt:Date(),sample:value,recoveredPhysicsDelta:delta,phone:phone.snapshot(rssi:rssi),correction:correction,correctedCoincidencesPerMinute:correction.corrected(sample:value,channel:-1))); data.append(10)
            let handle=try FileHandle(forWritingTo:url)
            defer { try? handle.close() }
            try handle.seekToEnd(); try handle.write(contentsOf:data); try handle.synchronize()
            return true
        } catch { issue="Phone log write failed: \(error.localizedDescription)"; return false }
    }
    private func publishActivity() {
        guard logging,let a=activity else { return }
        var state=MuonActivity.ContentState.waiting
        state.status=status; state.lastSeen=lastSeen
        if let s=sample {
            state.counts=s.counts; state.physics=s.physics; state.hasSample=s.hasSample
            state.temperature=s.temperature; state.pressure=s.pressure; state.device=s.deviceID
            state.sampleDate=lastSeen?.addingTimeInterval(-s.sampleAge)
        }
        let linkDeadline=(lastSeen ?? Date()).addingTimeInterval(90)
        let deadline=state.sampleDate.map { min(linkDeadline,$0.addingTimeInterval(90)) } ?? linkDeadline
        Task { await a.update(ActivityContent(state:state,staleDate:deadline)) }
    }
}

struct ChartSample: Identifiable {
    let id=UUID()
    let received:Date
    let sample:Telemetry
    var date:Date { received.addingTimeInterval(-sample.sampleAge) }
}
struct ControlError:LocalizedError {
    let message:String
    var errorDescription:String? { message }
}
extension Monitor {
    func restartLiveActivity() {
        guard logging,ActivityAuthorizationInfo().areActivitiesEnabled else { return }
        let old=activity
        do { activity=try Activity.request(attributes:MuonActivity(),content:ActivityContent(state:.waiting,staleDate:Date().addingTimeInterval(90)),pushType:nil);publishActivity() }
        catch { issue="Live Activity could not start: \(error.localizedDescription)";return }
        if let old { Task { await old.end(nil,dismissalPolicy:.immediate) } }
    }
    func cancelOperation() { finishControl(.failure(CancellationError())) }
    // One outstanding operation; a timeout never retries a hardware mutation.
    func command(_ op:String,_ values:[String:Any]=[:]) async throws -> [String:Any] {
        guard controlContinuation==nil else { throw ControlError(message:"Another detector operation is running") }
        guard controlsReady,let p=peripheral,p.state == .connected,let c=commandCharacteristic else { throw ControlError(message:"Connect to MuonP4 first") }
        requestID &+= 1;if requestID==0 { requestID=1 }
        var object=values;object["op"]=op;object["id"]=requestID
        let data=try JSONSerialization.data(withJSONObject:object)
        guard data.count<=240 else { throw ControlError(message:"Command is too long") }
        return try await withCheckedThrowingContinuation { continuation in
            controlContinuation=continuation
            let pendingID=requestID
            let timeout=DispatchWorkItem { [weak self] in
                guard let self,self.controlContinuation != nil,self.requestID==pendingID else { return }
                self.finishControl(.failure(ControlError(message:"No response. The operation may have completed; inspect status before retrying.")))
            }
            controlTimeout=timeout;DispatchQueue.main.asyncAfter(deadline:.now()+45,execute:timeout)
            p.writeValue(data,for:c,type:.withResponse)
        }
    }
    private func pollControl() {
        let id=requestID
        DispatchQueue.main.asyncAfter(deadline:.now()+0.5) { [weak self] in
            guard let self,self.controlContinuation != nil,self.requestID==id,let p=self.peripheral,let c=self.responseCharacteristic else { return }
            p.readValue(for:c)
        }
    }
    private func finishControl(_ result:Result<[String:Any],Error>) {
        controlTimeout?.cancel();controlTimeout=nil
        let pending=controlContinuation;controlContinuation=nil
        pending?.resume(with:result)
    }
    func peripheral(_ p:CBPeripheral,didWriteValueFor characteristic:CBCharacteristic,error:Error?) {
        guard characteristic.uuid==Self.controlID else { return }
        if let error { finishControl(.failure(error)) } else { pollControl() }
    }
    private func receiveControl(_ p:CBPeripheral,_ c:CBCharacteristic,_ error:Error?) {
        guard controlContinuation != nil else { return }
        if let error { finishControl(.failure(error));return }
        guard let data=c.value else { pollControl();return }
        if data == Data([79,75]) { p.readValue(for:c);return }
        guard let obj=(try? JSONSerialization.jsonObject(with:data)) as? [String:Any],(obj["id"] as? NSNumber)?.uint32Value==requestID else { pollControl();return }
        if obj["ok"] as? Bool == true { finishControl(.success(obj)) }
        else { finishControl(.failure(ControlError(message:obj["error"] as? String ?? "Detector rejected command"))) }
    }
    func download(name:String,progress:@escaping (Int,Int)->Void) async throws -> URL {
        let dir=FileManager.default.urls(for:.documentDirectory,in:.userDomainMask)[0]
        let final=dir.appendingPathComponent("\(Int(Date().timeIntervalSince1970))-\(URL(fileURLWithPath:name).lastPathComponent)")
        let part=final.appendingPathExtension("partial")
        FileManager.default.createFile(atPath:part.path,contents:nil,attributes:[.protectionKey:FileProtectionType.completeUntilFirstUserAuthentication])
        let file=try FileHandle(forWritingTo:part)
        defer { try? file.close();try? FileManager.default.removeItem(at:part) }
        var offset=0,limit:Int?
        while true {
            try Task.checkCancellation()
            var args:[String:Any]=["name":name,"offset":offset];if let limit { args["limit"]=limit }
            let result=try await command("file",args)
            guard let raw=result["data"] as? String,let data=Data(base64Encoded:raw),let next=result["next"] as? Int,
                  let size=result["limit"] as? Int,next==offset+data.count,limit==nil || limit==size else { throw ControlError(message:"Invalid file response") }
            if limit==nil { limit=size }
            try file.write(contentsOf:data);offset=next;progress(offset,size)
            if result["eof"] as? Bool == true { break }
            guard !data.isEmpty else { throw ControlError(message:"File transfer stalled") }
        }
        try file.synchronize();try file.close();try FileManager.default.moveItem(at:part,to:final);return final
    }
    func downloadRAM() async throws -> URL {
        let meta=try await command("ram")
        guard let first=meta["first"] as? Int,let last=meta["last"] as? Int,let header=meta["csv"] as? String else { throw ControlError(message:"Invalid minute log") }
        var csv=header
        if first>0 && last>=first {
            for sequence in first...last {
                try Task.checkCancellation()
                let row=try await command("ram",["sequence":sequence])
                guard let line=row["csv"] as? String else { throw ControlError(message:"Missing minute record") }
                csv+=line
            }
        }
        let url=FileManager.default.urls(for:.documentDirectory,in:.userDomainMask)[0].appendingPathComponent("minute-log-\(Int(Date().timeIntervalSince1970)).csv")
        try csv.write(to:url,atomically:true,encoding:.utf8);return url
    }
}

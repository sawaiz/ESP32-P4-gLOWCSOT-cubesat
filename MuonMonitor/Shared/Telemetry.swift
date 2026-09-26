import Foundation

struct Telemetry: Codable, Equatable {
    static let size = 160
    static let channels = ["CH01", "CH02", "CH12", "CH012", "GPIO6", "GPIO5", "GPIO16"]
    let flags: UInt8
    let bootID: UInt64
    let sequence: UInt32
    let sampleUptime: UInt64
    let exposure: UInt64
    let epoch: UInt64
    let interval: UInt32
    let temperature: Double?
    let pressure: Double?
    let status: UInt16
    let hv: UInt8
    let totals: [UInt64]
    let counts: [UInt32]
    let deviceID: String
    let uptime: UInt64
    var hasSample: Bool { flags & 1 != 0 }
    var physics: Bool { flags & 2 != 0 }
    var physicsReady: Bool { flags & 64 != 0 }
    var transition: Bool { flags & 128 != 0 }
    var sampleAge: Double { Double(uptime >= sampleUptime ? uptime - sampleUptime : 0) / 1000 }
    var coincidenceCount: UInt64 { counts.prefix(3).reduce(0) { $0 + UInt64($1) } }

    init?(data: Data) {
        let b = [UInt8](data)
        guard b.count == Self.size, b[0] == 77, b[1] == 80, b[2] == 4 else { return nil }
        func u(_ offset: Int, _ n: Int) -> UInt64 {
            (0..<n).reduce(0) { $0 | UInt64(b[offset + $1]) << (8 * $1) }
        }
        flags=b[3]; bootID=u(4,8); sequence=UInt32(u(12,4))
        sampleUptime=u(16,8); exposure=u(24,8); epoch=u(32,8); interval=UInt32(u(40,4))
        let t=Int32(bitPattern: UInt32(u(44,4))), p=UInt32(u(48,4))
        temperature = flags & 4 != 0 && t != Int32.min ? Double(t)/1000 : nil
        pressure = flags & 4 != 0 && p != UInt32.max ? Double(p)/100 : nil
        status=UInt16(u(52,2)); hv=b[54]
        totals=(0..<7).map { u(56+8*$0,8) }
        counts=(0..<7).map { UInt32(u(112+4*$0,4)) }
        deviceID=b[140..<146].map { String(format:"%02X",$0) }.joined(separator: ":")
        uptime=u(146,8)
        guard sampleUptime <= uptime, exposure <= uptime,
              !hasSample || interval >= 60000 else { return nil }
    }

    // Nil denotes a new boot/device or no new valid exposure, never zero counts.
    func delta(from old: Telemetry) -> PhysicsDelta? {
        guard deviceID == old.deviceID, bootID == old.bootID,
              uptime >= old.uptime, exposure > old.exposure,
              zip(totals,old.totals).allSatisfy({ $0 >= $1 }) else { return nil }
        return PhysicsDelta(counts:zip(totals,old.totals).map(-), exposureMS:exposure-old.exposure)
    }
}
struct PhysicsDelta: Codable, Equatable {
    let counts: [UInt64]
    let exposureMS: UInt64
    var coincidencesPerMinute: Double {
        Double(counts.prefix(3).reduce(0,+)) * 60000 / Double(exposureMS)
    }
}

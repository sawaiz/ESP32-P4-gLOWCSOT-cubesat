import ActivityKit
import Foundation

struct MuonActivity: ActivityAttributes {
    struct ContentState: Codable, Hashable {
        var status: String
        var counts: [UInt32]
        var temperature: Double?
        var pressure: Double?
        var physics: Bool
        var hasSample: Bool
        var lastSeen: Date?
        var sampleDate: Date?
        var device: String
        static var waiting: Self { .init(status:"Looking for MuonP4",counts:[],physics:false,hasSample:false,device:"") }
        var rateText: String { hasSample ? String(counts.prefix(3).reduce(UInt64(0)) { $0 + UInt64($1) }) : "—" }
    }
    var name: String = "MuonP4"
}

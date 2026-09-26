import Foundation
#if DEBUG
extension Monitor {
    func loadDemo() {
        demo=true;status="Demo · simulated measurements"
        let end=Date()
        for minute in 1...90 {
            var bytes=Data(repeating:0,count:160)
            func put(_ at:Int,_ value:UInt64,_ width:Int) { for i in 0..<width { bytes[at+i]=UInt8(truncatingIfNeeded:value>>(8*i)) } }
            bytes[0]=77;bytes[1]=80;bytes[2]=4;bytes[3]=127;bytes[54]=234
            put(4,123456,8);put(12,UInt64(minute),4);put(16,UInt64(minute*60000),8)
            put(24,UInt64(minute*60000),8);put(40,60000,4);put(146,UInt64(minute*60000),8)
            put(44,UInt64(24000+minute*12),4);put(48,UInt64(98200+minute*3),4);put(52,7,2)
            let counts=[20+minute%8,14+minute%5,21+minute%7,8+minute%3,340,310,360]
            for i in 0..<7 { put(112+i*4,UInt64(counts[i]),4);put(56+i*8,UInt64(counts[i]*minute),8) }
            if let value=Telemetry(data:bytes) {
                history.append(ChartSample(received:end.addingTimeInterval(Double(minute-90)*60),sample:value))
                sample=value
            }
        }
        lastSeen=end
    }
}
#endif

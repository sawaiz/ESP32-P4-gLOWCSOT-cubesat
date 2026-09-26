import SwiftUI
import Charts

struct ChartsView:View {
    @ObservedObject var monitor:Monitor
    @State private var channel = -1
    private struct Point:Identifiable {
        let id:String,date:Date,raw:Double,corrected:Double?,segment:String
    }
    private var points:[Point] {
        var result:[Point]=[],previous:Telemetry?,segment=0
        for row in monitor.history {
            let s=row.sample
            guard s.hasSample,s.physics,s.interval>0 else { segment+=1;previous=nil;continue }
            if let previous,previous.bootID != s.bootID || previous.deviceID != s.deviceID || s.sequence != previous.sequence &+ 1 { segment+=1 }
            let n=channel == -1 ? Double(s.coincidenceCount):Double(s.counts[channel])
            result.append(Point(id:row.id.uuidString,date:row.date,raw:n*60000/Double(s.interval),corrected:monitor.correction.corrected(sample:s,channel:channel),segment:String(segment)))
            previous=s
        }
        return result
    }
    var body:some View {
        NavigationStack {
            ScrollView {
                VStack(alignment:.leading,spacing:20) {
                    if monitor.demo { Text("SIMULATED PREVIEW").font(.caption).foregroundStyle(.orange) }
                    Picker("Channel",selection:$channel) {
                        Text("CH01 + CH02 + CH12").tag(-1)
                        ForEach(0..<7) { Text(Telemetry.channels[$0]).tag($0) }
                    }.pickerStyle(.menu)
                    Text("Raw and corrected physics rate").font(.title2.bold())
                    if points.isEmpty {
                        ContentUnavailableView("Waiting for physics data",systemImage:"chart.xyaxis.line",description:Text("Charts begin after a complete valid minute. Setup and HV settling are excluded."))
                    } else {
                        Chart(points) { p in
                            LineMark(x:.value("Time",p.date),y:.value("Counts/min",p.raw),series:.value("Segment","Raw"+p.segment)).foregroundStyle(by:.value("Series","Raw"))
                            PointMark(x:.value("Time",p.date),y:.value("Counts/min",p.raw)).foregroundStyle(by:.value("Series","Raw"))
                            if let corrected=p.corrected {
                                LineMark(x:.value("Time",p.date),y:.value("Counts/min",corrected),series:.value("Segment","Corrected"+p.segment)).foregroundStyle(by:.value("Series","Corrected"))
                                PointMark(x:.value("Time",p.date),y:.value("Counts/min",corrected)).foregroundStyle(by:.value("Series","Corrected"))
                            }
                        }.chartForegroundStyleScale(["Raw":Color.gray,"Corrected":Color.cyan]).frame(height:270)
                    }
                    Text("Corrections use the selected reference conditions and empirical coefficients. Temperature fits are provisional and consistent with zero. Missing minutes remain gaps; cumulative recovery does not invent missing environmental measurements.").font(.caption).foregroundStyle(.secondary)
                    if channel>2 { Text("No temperature coefficient was supplied for this channel. Its chart is raw only.").font(.caption).foregroundStyle(.orange) }
                    GroupBox("Pressure · hPa") {
                        Chart(monitor.history.filter { $0.sample.pressure != nil }) { row in
                            PointMark(x:.value("Time",row.date),y:.value("hPa",row.sample.pressure!)).foregroundStyle(.blue)
                        }.frame(height:150).chartYScale(domain:.automatic(includesZero:false))
                    }
                    GroupBox("Detector temperature · °C") {
                        Chart(monitor.history.filter { $0.sample.temperature != nil }) { row in
                            PointMark(x:.value("Time",row.date),y:.value("°C",row.sample.temperature!)).foregroundStyle(.orange)
                        }.frame(height:150).chartYScale(domain:.automatic(includesZero:false))
                    }
                    Text("Displays up to the latest 1,440 received minute records in this app session. The exported log retains the complete phone session.").font(.caption).foregroundStyle(.secondary)
                    Text("Recent minute log").font(.headline)
                    ForEach(monitor.history.suffix(20).reversed()) { row in
                        HStack {
                            VStack(alignment:.leading) { Text(row.date,style:.time);Text(row.sample.physics ? "Physics" : "Setup").font(.caption).foregroundStyle(row.sample.physics ? .cyan : .orange) }
                            Spacer();Text(row.sample.counts.map(String.init).joined(separator:" · ")).font(.caption.monospaced())
                        }
                    }
                }.padding()
            }.navigationTitle("Charts")
        }
    }
}

import ActivityKit
import WidgetKit
import SwiftUI

struct Readings: View {
    let state: MuonActivity.ContentState
    let stale: Bool
    var body: some View {
        VStack(alignment:.leading,spacing:10) {
            HStack {
                Label("MuonP4",systemImage:"sparkles").font(.headline)
                Spacer()
                Text(stale ? "Update overdue" : state.status).font(.caption).foregroundStyle(stale ? .orange : .secondary)
            }
            HStack {
                ForEach(0..<3) { i in
                    VStack(alignment:.leading) {
                        Text(["CH01","CH02","CH12"][i]).font(.caption).foregroundStyle(.secondary)
                        Text(state.hasSample && state.counts.count > i ? "\(state.counts[i])" : "—").font(.title2.monospacedDigit())
                    }.frame(maxWidth:.infinity,alignment:.leading)
                }
                VStack(alignment:.trailing) {
                    Text(state.temperature.map { String(format:"%.1f °C",$0) } ?? "— °C")
                    Text(state.pressure.map { String(format:"%.1f hPa",$0) } ?? "— hPa")
                }.font(.caption.monospacedDigit())
            }
            HStack {
                Text(state.hasSample ? (state.physics ? "Last completed minute · Physics" : "Last completed minute · Setup") : "Waiting for a complete minute")
                Spacer()
                if let date=state.sampleDate { Text("Sample ") + Text(date,style:.relative) + Text(" ago") }
            }.font(.caption2).foregroundStyle(.secondary)
        }.padding(16)
    }
}
@main struct MuonWidgets: WidgetBundle {
    var body: some Widget { MuonLiveActivity() }
}
struct MuonLiveActivity: Widget {
    var body: some WidgetConfiguration {
        ActivityConfiguration(for:MuonActivity.self) { context in
            Readings(state:context.state,stale:context.isStale)
                .activityBackgroundTint(Color(red:0.06,green:0.09,blue:0.13))
                .activitySystemActionForegroundColor(.white)
                .environment(\.colorScheme,.dark)
        } dynamicIsland: { context in
            DynamicIsland {
                DynamicIslandExpandedRegion(.leading) { Label("MuonP4",systemImage:"sparkles").font(.headline) }
                DynamicIslandExpandedRegion(.trailing) {
                    Text(context.isStale ? "Overdue" : context.state.status).font(.caption).foregroundStyle(context.isStale ? .orange : .cyan)
                }
                DynamicIslandExpandedRegion(.bottom) { Readings(state:context.state,stale:context.isStale) }
            } compactLeading: {
                Image(systemName:"sparkles").foregroundStyle(context.isStale ? .orange : .cyan)
            } compactTrailing: {
                Text(context.isStale ? "—" : context.state.rateText).monospacedDigit()
            } minimal: {
                Image(systemName:context.isStale ? "antenna.radiowaves.left.and.right.slash" : "sparkles")
            }
            .keylineTint(.cyan)
        }
    }
}

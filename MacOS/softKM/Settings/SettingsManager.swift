import Foundation
import SwiftUI

enum ScreenEdge: String, CaseIterable, Codable {
    case left, right, top, bottom
}

class SettingsManager: ObservableObject {
    static let shared = SettingsManager()

    @AppStorage("hostAddress") var hostAddress: String = "taurus.microgeni.synology.me"
    @AppStorage("port") var port: Int = 31337
    @AppStorage("useTLS") var useTLS: Bool = false
    @AppStorage("edgeDwellTime") var edgeDwellTime: Double = 0.3
    @AppStorage("edgeThreshold") var edgeThreshold: Double = 5.0

    // Monitor arrangement stored as JSON
    @AppStorage("monitorArrangement") private var arrangementData: Data = Data()

    var monitorArrangement: MonitorArrangement {
        get {
            if arrangementData.isEmpty {
                return .default
            }
            do {
                return try JSONDecoder().decode(MonitorArrangement.self, from: arrangementData)
            } catch {
                LOG("Failed to decode monitor arrangement: \(error)")
                return .default
            }
        }
        set {
            do {
                arrangementData = try JSONEncoder().encode(newValue)
                objectWillChange.send()
            } catch {
                LOG("Failed to encode monitor arrangement: \(error)")
            }
        }
    }

    /// The edge on the Mac side where switching to Haiku occurs
    var switchEdge: ScreenEdge {
        monitorArrangement.connectedEdge.screenEdge
    }

    var portAsUInt16: UInt16 {
        UInt16(clamping: port)
    }

    var edgeThresholdAsCGFloat: CGFloat {
        CGFloat(edgeThreshold)
    }

    private init() {}
}

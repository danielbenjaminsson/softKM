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

    // Monitor arrangement stored as JSON string (String works better with @AppStorage than Data)
    @AppStorage("monitorArrangementJSON") private var arrangementJSON: String = ""

    var monitorArrangement: MonitorArrangement {
        get {
            if arrangementJSON.isEmpty {
                return .default
            }
            do {
                let data = Data(arrangementJSON.utf8)
                return try JSONDecoder().decode(MonitorArrangement.self, from: data)
            } catch {
                LOG("Failed to decode monitor arrangement: \(error)")
                return .default
            }
        }
        set {
            do {
                let data = try JSONEncoder().encode(newValue)
                if let json = String(data: data, encoding: .utf8) {
                    arrangementJSON = json
                    LOG("Saved monitor arrangement: \(json)")
                }
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

import Foundation
import SwiftUI

class SettingsManager: ObservableObject {
    static let shared = SettingsManager()

    @AppStorage("hostAddress") var hostAddress: String = "taurus.microgeni.synology.me"
    @AppStorage("port") var port: Int = 31337
    @AppStorage("useTLS") var useTLS: Bool = false
    @AppStorage("switchEdge") var switchEdge: ScreenEdge = .right
    @AppStorage("edgeDwellTime") var edgeDwellTime: Double = 0.3
    @AppStorage("edgeThreshold") var edgeThreshold: Double = 5.0

    var portAsUInt16: UInt16 {
        UInt16(clamping: port)
    }

    var edgeThresholdAsCGFloat: CGFloat {
        CGFloat(edgeThreshold)
    }

    private init() {}
}

extension ScreenEdge: RawRepresentable {
    init?(rawValue: String) {
        switch rawValue {
        case "left": self = .left
        case "right": self = .right
        case "top": self = .top
        case "bottom": self = .bottom
        default: return nil
        }
    }
}

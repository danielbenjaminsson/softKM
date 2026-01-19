import Foundation
import CoreGraphics

struct MonitorRect: Codable, Equatable {
    var x: CGFloat
    var y: CGFloat
    var width: CGFloat
    var height: CGFloat

    var frame: CGRect {
        CGRect(x: x, y: y, width: width, height: height)
    }

    init(x: CGFloat, y: CGFloat, width: CGFloat, height: CGFloat) {
        self.x = x
        self.y = y
        self.width = width
        self.height = height
    }

    init(frame: CGRect) {
        self.x = frame.origin.x
        self.y = frame.origin.y
        self.width = frame.size.width
        self.height = frame.size.height
    }
}

enum ConnectedEdge: String, Codable, CaseIterable {
    case right
    case left
    case top
    case bottom
    case none

    var opposite: ConnectedEdge {
        switch self {
        case .right: return .left
        case .left: return .right
        case .top: return .bottom
        case .bottom: return .top
        case .none: return .none
        }
    }

    var screenEdge: ScreenEdge {
        switch self {
        case .right: return .right
        case .left: return .left
        case .top: return .top
        case .bottom: return .bottom
        case .none: return .right  // Default fallback
        }
    }

    var wireValue: UInt8 {
        switch self {
        case .right: return 0
        case .left: return 1
        case .top: return 2
        case .bottom: return 3
        case .none: return 0
        }
    }

    init(wireValue: UInt8) {
        switch wireValue {
        case 0: self = .right
        case 1: self = .left
        case 2: self = .top
        case 3: self = .bottom
        default: self = .none
        }
    }
}

struct MonitorArrangement: Codable, Equatable {
    var macMonitor: MonitorRect
    var haikuMonitor: MonitorRect

    /// The edge of the Mac monitor where Haiku is connected
    var connectedEdge: ConnectedEdge {
        computeConnectedEdge()
    }

    /// The edge of the Haiku monitor where Mac is (opposite of connectedEdge)
    var returnEdge: ConnectedEdge {
        connectedEdge.opposite
    }

    /// The vertical overlap region between Mac and Haiku monitors (in arrangement coordinates)
    /// Returns (overlapTop, overlapBottom) - the Y range where both monitors overlap
    var verticalOverlap: (top: CGFloat, bottom: CGFloat) {
        let macTop = macMonitor.y
        let macBottom = macMonitor.y + macMonitor.height
        let haikuTop = haikuMonitor.y
        let haikuBottom = haikuMonitor.y + haikuMonitor.height

        let overlapTop = max(macTop, haikuTop)
        let overlapBottom = min(macBottom, haikuBottom)

        return (overlapTop, overlapBottom)
    }

    /// The overlap region expressed as ratios of Mac's height (0.0 to 1.0)
    /// - overlapTopRatio: where the overlap starts (from Mac's top)
    /// - overlapBottomRatio: where the overlap ends (from Mac's top)
    var overlapRatios: (topRatio: CGFloat, bottomRatio: CGFloat) {
        let (overlapTop, overlapBottom) = verticalOverlap
        let topRatio = (overlapTop - macMonitor.y) / macMonitor.height
        let bottomRatio = (overlapBottom - macMonitor.y) / macMonitor.height
        return (topRatio, bottomRatio)
    }

    /// Legacy Y offset ratio for protocol compatibility
    /// This is the offset of Haiku's BOTTOM edge from Mac's BOTTOM edge,
    /// as a ratio of Mac's monitor height.
    var yOffsetRatio: CGFloat {
        let macBottom = macMonitor.y + macMonitor.height
        let haikuBottom = haikuMonitor.y + haikuMonitor.height
        return (macBottom - haikuBottom) / macMonitor.height
    }

    static let snapThreshold: CGFloat = 20.0

    static var `default`: MonitorArrangement {
        // Mac on left, Haiku on right (default arrangement)
        MonitorArrangement(
            macMonitor: MonitorRect(x: 0, y: 0, width: 240, height: 150),
            haikuMonitor: MonitorRect(x: 240, y: 0, width: 210, height: 150)
        )
    }

    /// Computes which edge of the Mac monitor is connected to Haiku
    private func computeConnectedEdge() -> ConnectedEdge {
        let mac = macMonitor.frame
        let haiku = haikuMonitor.frame

        // Check right edge of Mac (Haiku to the right)
        if abs(mac.maxX - haiku.minX) < Self.snapThreshold {
            // Check vertical overlap
            let overlapY = min(mac.maxY, haiku.maxY) - max(mac.minY, haiku.minY)
            if overlapY > 0 {
                return .right
            }
        }

        // Check left edge of Mac (Haiku to the left)
        if abs(mac.minX - haiku.maxX) < Self.snapThreshold {
            let overlapY = min(mac.maxY, haiku.maxY) - max(mac.minY, haiku.minY)
            if overlapY > 0 {
                return .left
            }
        }

        // Check top edge of Mac (Haiku above)
        if abs(mac.maxY - haiku.minY) < Self.snapThreshold {
            let overlapX = min(mac.maxX, haiku.maxX) - max(mac.minX, haiku.minX)
            if overlapX > 0 {
                return .top
            }
        }

        // Check bottom edge of Mac (Haiku below)
        if abs(mac.minY - haiku.maxY) < Self.snapThreshold {
            let overlapX = min(mac.maxX, haiku.maxX) - max(mac.minX, haiku.minX)
            if overlapX > 0 {
                return .bottom
            }
        }

        return .none
    }

    /// Snaps the Haiku monitor to the nearest edge of Mac monitor if within threshold
    /// Only snaps the edge coordinate, preserving the other axis position
    mutating func snapHaikuToMac() {
        let mac = macMonitor.frame
        var haiku = haikuMonitor.frame

        // Calculate distances to each edge
        let distToRight = abs(haiku.minX - mac.maxX)
        let distToLeft = abs(haiku.maxX - mac.minX)
        let distToTop = abs(haiku.minY - mac.maxY)
        let distToBottom = abs(haiku.maxY - mac.minY)

        let minDist = min(distToRight, distToLeft, distToTop, distToBottom)

        if minDist > Self.snapThreshold {
            return  // Too far to snap
        }

        if minDist == distToRight {
            // Snap to right edge - only adjust X, keep Y position
            haiku.origin.x = mac.maxX
        } else if minDist == distToLeft {
            // Snap to left edge - only adjust X, keep Y position
            haiku.origin.x = mac.minX - haiku.width
        } else if minDist == distToTop {
            // Snap to top edge - only adjust Y, keep X position
            haiku.origin.y = mac.maxY
        } else if minDist == distToBottom {
            // Snap to bottom edge - only adjust Y, keep X position
            haiku.origin.y = mac.minY - haiku.height
        }

        haikuMonitor = MonitorRect(frame: haiku)
    }

    /// Creates an arrangement with actual screen sizes scaled to fit
    static func withActualScreenSizes(
        macScreenSize: CGSize,
        haikuScreenSize: CGSize,
        baseHeight: CGFloat = 120
    ) -> MonitorArrangement {
        // Scale both monitors proportionally based on a target height
        let macScale = baseHeight / macScreenSize.height
        let haikuScale = baseHeight / haikuScreenSize.height

        // Use the smaller scale to ensure both fit well
        let scale = min(macScale, haikuScale)

        let macWidth = macScreenSize.width * scale
        let macHeight = macScreenSize.height * scale
        let haikuWidth = haikuScreenSize.width * scale
        let haikuHeight = haikuScreenSize.height * scale

        // Position Mac at origin, Haiku to the right (default)
        // Bottom-align the monitors
        let macY: CGFloat = 0
        let haikuY = macHeight - haikuHeight  // Bottom-align

        return MonitorArrangement(
            macMonitor: MonitorRect(x: 0, y: macY, width: macWidth, height: macHeight),
            haikuMonitor: MonitorRect(x: macWidth, y: haikuY, width: haikuWidth, height: haikuHeight)
        )
    }
}

import Cocoa

class EdgeDetector {
    // Read settings directly so changes take effect immediately
    private var settings: SettingsManager { SettingsManager.shared }
    var edgeThreshold: CGFloat { settings.edgeThresholdAsCGFloat }
    var activationDelay: TimeInterval { settings.edgeDwellTime }
    var activeEdge: ScreenEdge {
        settings.switchEdge
    }

    private var edgeEntryTime: Date?
    private var lastPosition: CGPoint = .zero

    enum SwitchDirection {
        case toHaiku
        case toMac
    }

    func checkMousePosition(_ position: CGPoint, currentMode: SwitchController.Mode) -> SwitchDirection? {
        lastPosition = position

        guard let screen = NSScreen.main else { return nil }
        let frame = screen.frame

        let atEdge = isAtEdge(position, in: frame, edge: activeEdge)
        let atOppositeEdge = isAtEdge(position, in: frame, edge: oppositeEdge(activeEdge))

        switch currentMode {
        case .monitoring:
            if atEdge {
                if edgeEntryTime == nil {
                    edgeEntryTime = Date()
                } else if Date().timeIntervalSince(edgeEntryTime!) >= activationDelay {
                    edgeEntryTime = nil
                    return .toHaiku
                }
            } else {
                edgeEntryTime = nil
            }

        case .capturing:
            if atOppositeEdge {
                if edgeEntryTime == nil {
                    edgeEntryTime = Date()
                } else if Date().timeIntervalSince(edgeEntryTime!) >= activationDelay {
                    edgeEntryTime = nil
                    return .toMac
                }
            } else {
                edgeEntryTime = nil
            }
        }

        return nil
    }

    func reset() {
        edgeEntryTime = nil
    }

    private func isAtEdge(_ position: CGPoint, in frame: CGRect, edge: ScreenEdge) -> Bool {
        // Get overlap region from monitor arrangement
        let arrangement = settings.monitorArrangement
        let (topRatio, bottomRatio) = arrangement.overlapRatios

        // Convert overlap ratios to screen coordinates
        // overlapRatios are top-down (0.0 = top), but Cocoa Y is bottom-up
        let overlapMinY = frame.minY + (1.0 - bottomRatio) * frame.height
        let overlapMaxY = frame.minY + (1.0 - topRatio) * frame.height

        switch edge {
        case .right:
            // Check X at edge AND Y within overlap region
            return position.x >= frame.maxX - edgeThreshold &&
                   position.y >= overlapMinY && position.y <= overlapMaxY
        case .left:
            return position.x <= frame.minX + edgeThreshold &&
                   position.y >= overlapMinY && position.y <= overlapMaxY
        case .top:
            // For top/bottom edges, check X within overlap (horizontal overlap)
            let overlapMinX = frame.minX + topRatio * frame.width
            let overlapMaxX = frame.minX + bottomRatio * frame.width
            return position.y >= frame.maxY - edgeThreshold &&
                   position.x >= overlapMinX && position.x <= overlapMaxX
        case .bottom:
            let overlapMinX = frame.minX + topRatio * frame.width
            let overlapMaxX = frame.minX + bottomRatio * frame.width
            return position.y <= frame.minY + edgeThreshold &&
                   position.x >= overlapMinX && position.x <= overlapMaxX
        }
    }

    private func oppositeEdge(_ edge: ScreenEdge) -> ScreenEdge {
        switch edge {
        case .right: return .left
        case .left: return .right
        case .top: return .bottom
        case .bottom: return .top
        }
    }

    func getEdgePoint() -> CGPoint {
        guard let screen = NSScreen.main else { return .zero }
        let frame = screen.frame

        switch activeEdge {
        case .right:
            return CGPoint(x: frame.maxX - 1, y: frame.midY)
        case .left:
            return CGPoint(x: frame.minX + 1, y: frame.midY)
        case .top:
            return CGPoint(x: frame.midX, y: frame.maxY - 1)
        case .bottom:
            return CGPoint(x: frame.midX, y: frame.minY + 1)
        }
    }
}

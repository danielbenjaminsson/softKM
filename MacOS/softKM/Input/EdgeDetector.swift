import Cocoa

class EdgeDetector {
    var edgeThreshold: CGFloat = 5.0
    var activationDelay: TimeInterval = 0.3
    var activeEdge: ScreenEdge = .right

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
        switch edge {
        case .right:
            return position.x >= frame.maxX - edgeThreshold
        case .left:
            return position.x <= frame.minX + edgeThreshold
        case .top:
            return position.y >= frame.maxY - edgeThreshold
        case .bottom:
            return position.y <= frame.minY + edgeThreshold
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

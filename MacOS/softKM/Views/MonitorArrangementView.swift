import SwiftUI
import AppKit

struct MonitorArrangementView: View {
    @Binding var arrangement: MonitorArrangement
    var macScreenSize: CGSize
    var haikuScreenSize: CGSize

    private let arrangementHeight: CGFloat = 220

    @State private var isDragging = false
    @State private var dragOffset: CGSize = .zero
    @State private var hasInitializedSizes = false

    var body: some View {
        VStack(spacing: 12) {
            GeometryReader { geometry in
                let arrangementSize = CGSize(width: geometry.size.width, height: arrangementHeight)

                ZStack {
                    // Background
                    RoundedRectangle(cornerRadius: 8)
                        .fill(Color(NSColor.windowBackgroundColor).opacity(0.5))

                    // Mac monitor (fixed)
                    MonitorView(
                        label: "Mac",
                        icon: "laptopcomputer",
                        frame: scaledFrame(arrangement.macMonitor, in: arrangementSize),
                        isConnected: arrangement.connectedEdge != .none,
                        connectedEdge: nil,
                        showInputIcons: true
                    )

                    // Haiku monitor (draggable)
                    MonitorView(
                        label: "Haiku",
                        icon: "desktopcomputer",
                        frame: scaledFrame(arrangement.haikuMonitor, in: arrangementSize),
                        isConnected: arrangement.connectedEdge != .none,
                        connectedEdge: arrangement.connectedEdge
                    )
                    .offset(isDragging ? dragOffset : .zero)
                    .gesture(
                        DragGesture()
                            .onChanged { value in
                                isDragging = true
                                dragOffset = value.translation
                            }
                            .onEnded { value in
                                isDragging = false
                                applyDrag(value.translation)
                                dragOffset = .zero
                            }
                    )

                    // Connection indicator line
                    if arrangement.connectedEdge != .none && !isDragging {
                        ConnectionLine(
                            macFrame: scaledFrame(arrangement.macMonitor, in: arrangementSize),
                            haikuFrame: scaledFrame(arrangement.haikuMonitor, in: arrangementSize),
                            edge: arrangement.connectedEdge
                        )
                    }
                }
                .clipped()
            }
            .frame(height: arrangementHeight)

            // Caption showing connected edge
            Text(edgeDescription)
                .font(.callout)
                .foregroundColor(.secondary)
        }
        .onAppear {
            initializeWithActualSizes()
        }
    }

    private func initializeWithActualSizes() {
        // Only initialize if we haven't already and have valid screen sizes
        guard !hasInitializedSizes,
              macScreenSize.width > 0, macScreenSize.height > 0,
              haikuScreenSize.width > 0, haikuScreenSize.height > 0 else {
            return
        }

        // Check if arrangement has default/equal sizes that need updating
        let currentMacSize = CGSize(width: arrangement.macMonitor.width, height: arrangement.macMonitor.height)
        let currentHaikuSize = CGSize(width: arrangement.haikuMonitor.width, height: arrangement.haikuMonitor.height)

        // If sizes look like defaults (roughly equal), update to actual proportions
        let sizeDiff = abs(currentMacSize.width - currentHaikuSize.width)
        if sizeDiff < 30 {  // Likely using default equal-ish sizes
            let newArrangement = MonitorArrangement.withActualScreenSizes(
                macScreenSize: macScreenSize,
                haikuScreenSize: haikuScreenSize
            )
            arrangement = newArrangement
        }
        hasInitializedSizes = true
    }

    private var edgeDescription: String {
        switch arrangement.connectedEdge {
        case .right:
            return "Haiku is to the right of Mac"
        case .left:
            return "Haiku is to the left of Mac"
        case .top:
            return "Haiku is above Mac"
        case .bottom:
            return "Haiku is below Mac"
        case .none:
            return "Drag Haiku monitor to snap to Mac"
        }
    }

    private func scaledFrame(_ monitor: MonitorRect, in arrangementSize: CGSize) -> CGRect {
        // Scale from arrangement coordinates to view coordinates
        // Center the arrangement in the view
        let scale: CGFloat = 1.0
        let centerX = arrangementSize.width / 2
        let centerY = arrangementSize.height / 2

        // Calculate the center of both monitors combined
        let combinedMinX = min(arrangement.macMonitor.x, arrangement.haikuMonitor.x)
        let combinedMaxX = max(arrangement.macMonitor.x + arrangement.macMonitor.width,
                               arrangement.haikuMonitor.x + arrangement.haikuMonitor.width)
        let combinedMinY = min(arrangement.macMonitor.y, arrangement.haikuMonitor.y)
        let combinedMaxY = max(arrangement.macMonitor.y + arrangement.macMonitor.height,
                               arrangement.haikuMonitor.y + arrangement.haikuMonitor.height)

        let combinedCenterX = (combinedMinX + combinedMaxX) / 2
        let combinedCenterY = (combinedMinY + combinedMaxY) / 2

        let offsetX = centerX - combinedCenterX * scale
        let offsetY = centerY - combinedCenterY * scale

        return CGRect(
            x: monitor.x * scale + offsetX,
            y: monitor.y * scale + offsetY,
            width: monitor.width * scale,
            height: monitor.height * scale
        )
    }

    private func applyDrag(_ translation: CGSize) {
        // Convert screen translation to arrangement coordinates
        let newHaikuX = arrangement.haikuMonitor.x + translation.width
        let newHaikuY = arrangement.haikuMonitor.y + translation.height

        // Update arrangement with new position
        var newArrangement = arrangement
        newArrangement.haikuMonitor.x = newHaikuX
        newArrangement.haikuMonitor.y = newHaikuY

        // Snap to edges
        newArrangement.snapHaikuToMac()

        arrangement = newArrangement
    }
}

struct MonitorView: View {
    let label: String
    let icon: String
    let frame: CGRect
    let isConnected: Bool
    let connectedEdge: ConnectedEdge?
    var showInputIcons: Bool = false

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 6)
                .fill(Color(NSColor.darkGray))
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(isConnected ? Color.accentColor : Color.gray, lineWidth: 2)
                )

            VStack(spacing: 4) {
                Image(systemName: icon)
                    .font(.system(size: 20))
                    .foregroundColor(.white)

                Text(label)
                    .font(.caption)
                    .fontWeight(.medium)
                    .foregroundColor(.white)
            }

            // Input icons (keyboard + mouse) in bottom left
            if showInputIcons {
                HStack(spacing: 2) {
                    Image(systemName: "keyboard")
                        .font(.system(size: 9))
                    Image(systemName: "computermouse")
                        .font(.system(size: 9))
                }
                .foregroundColor(.white.opacity(0.7))
                .position(x: 18, y: frame.height - 10)
            }
        }
        .frame(width: frame.width, height: frame.height)
        .position(x: frame.midX, y: frame.midY)
    }
}

struct ConnectionLine: View {
    let macFrame: CGRect
    let haikuFrame: CGRect
    let edge: ConnectedEdge

    var body: some View {
        Path { path in
            let (start, end) = connectionPoints
            path.move(to: start)
            path.addLine(to: end)
        }
        .stroke(Color.accentColor, style: StrokeStyle(lineWidth: 3, lineCap: .round))
    }

    private var connectionPoints: (CGPoint, CGPoint) {
        switch edge {
        case .right:
            // Line on right edge of Mac / left edge of Haiku
            let y = max(macFrame.minY, haikuFrame.minY) +
                    (min(macFrame.maxY, haikuFrame.maxY) - max(macFrame.minY, haikuFrame.minY)) / 2
            return (CGPoint(x: macFrame.maxX, y: y),
                    CGPoint(x: haikuFrame.minX, y: y))
        case .left:
            // Line on left edge of Mac / right edge of Haiku
            let y = max(macFrame.minY, haikuFrame.minY) +
                    (min(macFrame.maxY, haikuFrame.maxY) - max(macFrame.minY, haikuFrame.minY)) / 2
            return (CGPoint(x: macFrame.minX, y: y),
                    CGPoint(x: haikuFrame.maxX, y: y))
        case .top:
            // Line on top edge of Mac / bottom edge of Haiku
            let x = max(macFrame.minX, haikuFrame.minX) +
                    (min(macFrame.maxX, haikuFrame.maxX) - max(macFrame.minX, haikuFrame.minX)) / 2
            return (CGPoint(x: x, y: macFrame.maxY),
                    CGPoint(x: x, y: haikuFrame.minY))
        case .bottom:
            // Line on bottom edge of Mac / top edge of Haiku
            let x = max(macFrame.minX, haikuFrame.minX) +
                    (min(macFrame.maxX, haikuFrame.maxX) - max(macFrame.minX, haikuFrame.minX)) / 2
            return (CGPoint(x: x, y: macFrame.minY),
                    CGPoint(x: x, y: haikuFrame.maxY))
        case .none:
            return (.zero, .zero)
        }
    }
}

#Preview {
    MonitorArrangementView(
        arrangement: .constant(.default),
        macScreenSize: CGSize(width: 1920, height: 1080),
        haikuScreenSize: CGSize(width: 1600, height: 900)
    )
    .padding()
}

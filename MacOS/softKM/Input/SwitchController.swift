import Cocoa
import CoreGraphics

class SwitchController {
    static let shared = SwitchController()

    enum Mode {
        case monitoring
        case capturing
    }

    private(set) var mode: Mode = .monitoring
    private let edgeDetector = EdgeDetector()
    private var lastMousePosition: CGPoint = .zero
    private var lockedCursorPosition: CGPoint = .zero  // Position to lock cursor during capture
    private var connectionManager: ConnectionManager { ConnectionManager.shared }

    private init() {
        // Load settings
        let settings = SettingsManager.shared
        edgeDetector.activeEdge = settings.switchEdge
        edgeDetector.activationDelay = settings.edgeDwellTime
        edgeDetector.edgeThreshold = settings.edgeThresholdAsCGFloat

        // Ensure cursor is visible on startup (reset any stale state)
        CGAssociateMouseAndMouseCursorPosition(1)
        CGDisplayShowCursor(CGMainDisplayID())

        // Listen for switch-to-mac notifications from Haiku
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(handleSwitchToMac),
            name: .switchToMac,
            object: nil
        )
    }

    @objc private func handleSwitchToMac(_ notification: Notification) {
        var yFromBottom: Float = 0.0
        if let info = notification.object as? SwitchToMacInfo {
            yFromBottom = info.yFromBottom
        }
        LOG("Received switch-to-mac notification from Haiku, yFromBottom=\(yFromBottom)")
        deactivateCaptureMode(yFromBottom: yFromBottom)
    }

    func handleEvent(type: CGEventType, event: CGEvent) -> Unmanaged<CGEvent>? {
        let location = event.location

        switch type {
        case .mouseMoved, .leftMouseDragged, .rightMouseDragged, .otherMouseDragged:
            return handleMouseMove(event: event, location: location)

        case .leftMouseDown, .rightMouseDown, .otherMouseDown:
            return handleMouseButton(event: event, isDown: true)

        case .leftMouseUp, .rightMouseUp, .otherMouseUp:
            return handleMouseButton(event: event, isDown: false)

        case .scrollWheel:
            return handleScrollWheel(event: event)

        case .keyDown:
            return handleKey(event: event, isDown: true)

        case .keyUp:
            return handleKey(event: event, isDown: false)

        case .flagsChanged:
            return handleFlagsChanged(event: event)

        default:
            return Unmanaged.passUnretained(event)
        }
    }

    private func handleMouseMove(event: CGEvent, location: CGPoint) -> Unmanaged<CGEvent>? {
        // Debug: log position near edges
        if let screen = NSScreen.main {
            let frame = screen.frame
            if location.x >= frame.maxX - 20 || location.x <= frame.minX + 20 {
                LOG("Mouse near edge: x=\(location.x) frame.maxX=\(frame.maxX) mode=\(mode)")
            }
        }

        // Check for edge switching
        if let direction = edgeDetector.checkMousePosition(location, currentMode: mode) {
            switch direction {
            case .toHaiku:
                activateCaptureMode()
            case .toMac:
                deactivateCaptureMode()
            }
        }

        switch mode {
        case .monitoring:
            return Unmanaged.passUnretained(event)

        case .capturing:
            // Use raw device deltas from the event
            let deltaX = Float(event.getDoubleValueField(.mouseEventDeltaX))
            let deltaY = Float(event.getDoubleValueField(.mouseEventDeltaY))

            if deltaX != 0 || deltaY != 0 {
                LOG("Mouse delta: (\(deltaX), \(deltaY))")
                connectionManager.send(event: .mouseMove(x: deltaX, y: deltaY, relative: true))
            }

            // Keep cursor locked at the edge position
            CGWarpMouseCursorPosition(lockedCursorPosition)

            return nil  // Consume event
        }
    }

    private func handleMouseButton(event: CGEvent, isDown: Bool) -> Unmanaged<CGEvent>? {
        guard mode == .capturing else {
            return Unmanaged.passUnretained(event)
        }

        let buttons = mapMouseButtons(event: event)
        let location = event.location

        if isDown {
            LOG("MouseDown: buttons=0x\(String(format: "%02X", buttons))")
            connectionManager.send(event: .mouseDown(buttons: buttons, x: Float(location.x), y: Float(location.y)))
        } else {
            LOG("MouseUp: buttons=0x\(String(format: "%02X", buttons))")
            connectionManager.send(event: .mouseUp(buttons: buttons, x: Float(location.x), y: Float(location.y)))
        }

        return nil  // Consume event
    }

    private func handleScrollWheel(event: CGEvent) -> Unmanaged<CGEvent>? {
        guard mode == .capturing else {
            return Unmanaged.passUnretained(event)
        }

        let deltaX = Float(event.getDoubleValueField(.scrollWheelEventDeltaAxis2))
        let deltaY = Float(event.getDoubleValueField(.scrollWheelEventDeltaAxis1))

        LOG("ScrollWheel: delta=(\(deltaX), \(deltaY))")
        connectionManager.send(event: .mouseWheel(deltaX: deltaX, deltaY: deltaY))

        return nil  // Consume event
    }

    private func handleKey(event: CGEvent, isDown: Bool) -> Unmanaged<CGEvent>? {
        guard mode == .capturing else {
            return Unmanaged.passUnretained(event)
        }

        let keyCode = UInt32(event.getIntegerValueField(.keyboardEventKeycode))
        let modifiers = mapModifiers(event.flags)

        if isDown {
            let characters = getCharacters(from: event)
            connectionManager.send(event: .keyDown(keyCode: keyCode, modifiers: modifiers, characters: characters))
        } else {
            connectionManager.send(event: .keyUp(keyCode: keyCode, modifiers: modifiers))
        }

        return nil  // Consume event
    }

    private func handleFlagsChanged(event: CGEvent) -> Unmanaged<CGEvent>? {
        guard mode == .capturing else {
            return Unmanaged.passUnretained(event)
        }

        // Modifier key state changed - send as key event
        let keyCode = UInt32(event.getIntegerValueField(.keyboardEventKeycode))
        let modifiers = mapModifiers(event.flags)

        // Determine if this is a key down or up based on whether the modifier is now set
        let isDown = isModifierKeyDown(keyCode: keyCode, flags: event.flags)

        if isDown {
            connectionManager.send(event: .keyDown(keyCode: keyCode, modifiers: modifiers, characters: ""))
        } else {
            connectionManager.send(event: .keyUp(keyCode: keyCode, modifiers: modifiers))
        }

        return nil  // Consume event
    }

    private func activateCaptureMode() {
        guard connectionManager.isConnected else {
            LOG("Cannot activate capture mode - not connected")
            return
        }

        LOG("Activating capture mode - switching to Haiku")

        // Calculate Y position from bottom for smooth handoff (bottom-aligned monitors)
        var yFromBottom: Float = 0.0
        if let screen = NSScreen.main {
            let frame = screen.frame
            let mouseLocation = NSEvent.mouseLocation
            // NSEvent.mouseLocation uses bottom-left origin, so Y is already from bottom
            yFromBottom = Float(mouseLocation.y - frame.minY)

            LOG("MAC→HAIKU: mouseY=\(mouseLocation.y) frame.minY=\(frame.minY) frame.height=\(frame.height) yFromBottom=\(yFromBottom)")

            // Set and warp cursor to edge - this position will be maintained during capture
            lockedCursorPosition = CGPoint(x: frame.maxX - 1, y: mouseLocation.y)
            CGWarpMouseCursorPosition(lockedCursorPosition)
        }

        // Disconnect cursor from mouse movement (must be after warp)
        CGAssociateMouseAndMouseCursorPosition(0)

        // Hide cursor
        CGDisplayHideCursor(CGMainDisplayID())

        // Now set mode after cursor is locked
        mode = .capturing

        // Notify Haiku with Y position (pixels from bottom) for smooth cursor transition
        connectionManager.sendControlSwitch(toHaiku: true, yFromBottom: yFromBottom)
        LOG("Sending control switch with yFromBottom=\(yFromBottom)")

        DispatchQueue.main.async {
            ConnectionManager.shared.isCapturing = true
        }

        // Start event capture if not already
        _ = EventCapture.shared.startCapture()
    }

    private func deactivateCaptureMode(yFromBottom: Float = 0.0) {
        LOG("Deactivating capture mode - switching back to macOS, yFromBottom=\(yFromBottom)")
        mode = .monitoring

        // Reconnect cursor to mouse movement
        CGAssociateMouseAndMouseCursorPosition(1)

        // Show cursor
        CGDisplayShowCursor(CGMainDisplayID())

        // Position cursor based on yFromBottom from Haiku (bottom-aligned monitors)
        if let screen = NSScreen.main {
            let frame = screen.frame
            // Move cursor 100 pixels away from the right edge to prevent immediate re-trigger
            let newX = frame.maxX - 100

            // Calculate Y position from yFromBottom
            // Use same coordinate system as activateCaptureMode: newY = yFromBottom + frame.minY
            var newY = CGFloat(yFromBottom) + frame.minY
            // Clamp to screen bounds
            if newY < frame.minY { newY = frame.minY }
            if newY > frame.maxY - 1 { newY = frame.maxY - 1 }

            LOG("HAIKU→MAC: yFromBottom=\(yFromBottom) frame.height=\(frame.height) frame.minY=\(frame.minY) → newY=\(newY)")
            CGWarpMouseCursorPosition(CGPoint(x: newX, y: newY))
        }

        // Notify Haiku
        connectionManager.sendControlSwitch(toHaiku: false)

        DispatchQueue.main.async {
            ConnectionManager.shared.isCapturing = false
        }

        edgeDetector.reset()
    }

    private func mapMouseButtons(event: CGEvent) -> UInt32 {
        let type = event.type
        switch type {
        case .leftMouseDown, .leftMouseUp:
            return 0x01  // B_PRIMARY_MOUSE_BUTTON
        case .rightMouseDown, .rightMouseUp:
            return 0x02  // B_SECONDARY_MOUSE_BUTTON
        case .otherMouseDown, .otherMouseUp:
            return 0x04  // B_TERTIARY_MOUSE_BUTTON
        default:
            return 0
        }
    }

    private func mapModifiers(_ flags: CGEventFlags) -> UInt32 {
        var modifiers: UInt32 = 0

        if flags.contains(.maskShift) { modifiers |= 0x01 }
        if flags.contains(.maskControl) { modifiers |= 0x04 }
        if flags.contains(.maskAlternate) { modifiers |= 0x02 }  // Option/Alt
        if flags.contains(.maskCommand) { modifiers |= 0x40 }
        if flags.contains(.maskSecondaryFn) { modifiers |= 0x10 }
        if flags.contains(.maskAlphaShift) { modifiers |= 0x20 }  // Caps Lock

        return modifiers
    }

    private func isModifierKeyDown(keyCode: UInt32, flags: CGEventFlags) -> Bool {
        switch keyCode {
        case 56, 60: return flags.contains(.maskShift)      // Left/Right Shift
        case 59, 62: return flags.contains(.maskControl)    // Left/Right Control
        case 58, 61: return flags.contains(.maskAlternate)  // Left/Right Option
        case 55, 54: return flags.contains(.maskCommand)    // Left/Right Command
        case 57: return flags.contains(.maskAlphaShift)     // Caps Lock
        case 63: return flags.contains(.maskSecondaryFn)    // Fn
        default: return false
        }
    }

    private func getCharacters(from event: CGEvent) -> String {
        if let nsEvent = NSEvent(cgEvent: event) {
            return nsEvent.characters ?? ""
        }
        return ""
    }
}

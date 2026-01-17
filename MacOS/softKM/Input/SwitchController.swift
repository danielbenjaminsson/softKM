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
    private var connectionManager: ConnectionManager { ConnectionManager.shared }

    private init() {
        // Load settings
        let settings = SettingsManager.shared
        edgeDetector.activeEdge = settings.switchEdge
        edgeDetector.activationDelay = settings.edgeDwellTime
        edgeDetector.edgeThreshold = settings.edgeThresholdAsCGFloat
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
            // Send relative movement to Haiku
            let deltaX = Float(location.x - lastMousePosition.x)
            let deltaY = Float(location.y - lastMousePosition.y)
            lastMousePosition = location

            if deltaX != 0 || deltaY != 0 {
                connectionManager.send(event: .mouseMove(x: deltaX, y: deltaY, relative: true))
            }

            // Keep cursor trapped at edge
            let edgePoint = edgeDetector.getEdgePoint()
            CGWarpMouseCursorPosition(edgePoint)

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
            connectionManager.send(event: .mouseDown(buttons: buttons, x: Float(location.x), y: Float(location.y)))
        } else {
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
        guard connectionManager.isConnected else { return }

        mode = .capturing
        lastMousePosition = NSEvent.mouseLocation

        // Hide and disconnect cursor
        CGDisplayHideCursor(CGMainDisplayID())
        CGAssociateMouseAndMouseCursorPosition(0)

        // Notify Haiku
        connectionManager.sendControlSwitch(toHaiku: true)

        DispatchQueue.main.async {
            ConnectionManager.shared.isCapturing = true
        }

        // Start event capture if not already
        _ = EventCapture.shared.startCapture()
    }

    private func deactivateCaptureMode() {
        mode = .monitoring

        // Show and reconnect cursor
        CGAssociateMouseAndMouseCursorPosition(1)
        CGDisplayShowCursor(CGMainDisplayID())

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

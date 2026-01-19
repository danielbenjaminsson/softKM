import Cocoa
import CoreGraphics
import Carbon.HIToolbox

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

    private var mouseLogCounter = 0

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
            // Use raw device deltas from the event
            let deltaX = Float(event.getDoubleValueField(.mouseEventDeltaX))
            let deltaY = Float(event.getDoubleValueField(.mouseEventDeltaY))

            if deltaX != 0 || deltaY != 0 {
                mouseLogCounter += 1
                if mouseLogCounter % 100 == 1 {
                    LOG("Mouse delta: (\(deltaX), \(deltaY)) [event #\(mouseLogCounter)]")
                }
                let modifiers = mapModifiers(event.flags)
                connectionManager.send(event: .mouseMove(x: deltaX, y: deltaY, relative: true, modifiers: modifiers))
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
        let modifiers = mapModifiers(event.flags)
        // Get click count from macOS (1=single, 2=double, 3=triple, etc.)
        let clicks = isDown ? Int32(event.getIntegerValueField(.mouseEventClickState)) : 1

        if isDown {
            LOG("MouseDown: buttons=0x\(String(format: "%02X", buttons)) clicks=\(clicks) mods=0x\(String(format: "%02X", modifiers))")
            connectionManager.send(event: .mouseDown(buttons: buttons, x: Float(location.x), y: Float(location.y), modifiers: modifiers, clicks: clicks))
        } else {
            LOG("MouseUp: buttons=0x\(String(format: "%02X", buttons))")
            connectionManager.send(event: .mouseUp(buttons: buttons, x: Float(location.x), y: Float(location.y), modifiers: modifiers))
        }

        return nil  // Consume event
    }

    private func handleScrollWheel(event: CGEvent) -> Unmanaged<CGEvent>? {
        guard mode == .capturing else {
            return Unmanaged.passUnretained(event)
        }

        let deltaX = Float(event.getDoubleValueField(.scrollWheelEventDeltaAxis2))
        let deltaY = Float(event.getDoubleValueField(.scrollWheelEventDeltaAxis1))
        let modifiers = mapModifiers(event.flags)

        LOG("ScrollWheel: delta=(\(deltaX), \(deltaY))")
        connectionManager.send(event: .mouseWheel(deltaX: deltaX, deltaY: deltaY, modifiers: modifiers))

        return nil  // Consume event
    }

    private func handleKey(event: CGEvent, isDown: Bool) -> Unmanaged<CGEvent>? {
        guard mode == .capturing else {
            return Unmanaged.passUnretained(event)
        }

        let keyCode = UInt32(event.getIntegerValueField(.keyboardEventKeycode))
        let modifiers = mapModifiers(event.flags)
        let flags = event.flags

        // Check for Ctrl+Cmd+Delete -> Team Monitor on Haiku
        // macOS Delete (forward delete) is keycode 0x75, Backspace is 0x33
        if isDown && (keyCode == 0x75 || keyCode == 0x33) {
            let hasCtrl = flags.contains(.maskControl)
            let hasCmd = flags.contains(.maskCommand)
            if hasCtrl && hasCmd {
                LOG("Ctrl+Cmd+Delete detected - sending Team Monitor request")
                connectionManager.send(event: .teamMonitor)
                return nil
            }
        }

        if isDown {
            let characters = getCharacters(from: event)
            let bytesHex = characters.utf8.map { String(format: "%02X", $0) }.joined(separator: " ")
            LOG("KeyDown: keyCode=0x\(String(format: "%02X", keyCode)) mods=0x\(String(format: "%02X", modifiers)) chars='\(characters)' bytes=[\(bytesHex)]")
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

            // Scale yFromBottom from Haiku coordinates to macOS coordinates
            var scaledYFromBottom = CGFloat(yFromBottom)
            let remoteHeight = connectionManager.remoteScreenSize.height
            if remoteHeight > 0 {
                scaledYFromBottom = CGFloat(yFromBottom) * frame.height / remoteHeight
                LOG("Scaling yFromBottom: \(yFromBottom) * \(frame.height) / \(remoteHeight) = \(scaledYFromBottom)")
            }

            // Calculate Y position from scaled yFromBottom
            // Use same coordinate system as activateCaptureMode: newY = yFromBottom + frame.minY
            var newY = scaledYFromBottom + frame.minY
            // Clamp to screen bounds
            if newY < frame.minY { newY = frame.minY }
            if newY > frame.maxY - 1 { newY = frame.maxY - 1 }

            LOG("HAIKU→MAC: yFromBottom=\(yFromBottom) scaled=\(scaledYFromBottom) frame.height=\(frame.height) frame.minY=\(frame.minY) → newY=\(newY)")
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
            // Get the actual button number for "other" buttons
            // macOS: 2=middle, 3=button4 (back), 4=button5 (forward)
            let buttonNumber = event.getIntegerValueField(.mouseEventButtonNumber)
            switch buttonNumber {
            case 2:
                return 0x04  // B_TERTIARY_MOUSE_BUTTON (middle)
            case 3:
                return 0x08  // Button 4 (back)
            case 4:
                return 0x10  // Button 5 (forward)
            default:
                return 0x04  // Default to middle button
            }
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
        // Use CGEventKeyboardGetUnicodeString for more reliable character extraction
        // This works better with event taps than NSEvent.characters
        var length: Int = 0
        var chars = [UniChar](repeating: 0, count: 4)

        event.keyboardGetUnicodeString(
            maxStringLength: chars.count,
            actualStringLength: &length,
            unicodeString: &chars
        )

        if length > 0 {
            let result = String(utf16CodeUnits: chars, count: length)
            return result
        }

        // Fallback to NSEvent method if CGEvent method returns nothing
        if let nsEvent = NSEvent(cgEvent: event) {
            if let characters = nsEvent.characters, !characters.isEmpty {
                return characters
            }
        }

        // Use UCKeyTranslate for Option combinations and dead keys
        // This is needed for international keyboards (Swedish, German, etc.)
        let keyCode = UInt16(event.getIntegerValueField(.keyboardEventKeycode))
        let flags = event.flags

        if let result = translateKeyWithUCKey(keyCode: keyCode, flags: flags) {
            return result
        }

        return ""
    }

    private func translateKeyWithUCKey(keyCode: UInt16, flags: CGEventFlags) -> String? {
        // Try current keyboard input source first
        var inputSource = TISCopyCurrentKeyboardInputSource()?.takeRetainedValue()
        var layoutDataRef = inputSource.flatMap { TISGetInputSourceProperty($0, kTISPropertyUnicodeKeyLayoutData) }

        // If no layout data, try ASCII capable keyboard
        if layoutDataRef == nil {
            inputSource = TISCopyCurrentASCIICapableKeyboardInputSource()?.takeRetainedValue()
            layoutDataRef = inputSource.flatMap { TISGetInputSourceProperty($0, kTISPropertyUnicodeKeyLayoutData) }
            LOG("UCKeyTranslate: Falling back to ASCII capable keyboard")
        }

        guard let layoutDataRef = layoutDataRef else {
            LOG("UCKeyTranslate: No keyboard layout data available")
            return nil
        }

        let layoutData = unsafeBitCast(layoutDataRef, to: CFData.self)
        guard let keyboardLayout = CFDataGetBytePtr(layoutData) else {
            LOG("UCKeyTranslate: Could not get keyboard layout bytes")
            return nil
        }

        // Build modifier state
        var modifierState: UInt32 = 0
        if flags.contains(.maskShift) { modifierState |= UInt32(shiftKey >> 8) }
        if flags.contains(.maskAlternate) { modifierState |= UInt32(optionKey >> 8) }
        if flags.contains(.maskCommand) { modifierState |= UInt32(cmdKey >> 8) }
        if flags.contains(.maskControl) { modifierState |= UInt32(controlKey >> 8) }
        if flags.contains(.maskAlphaShift) { modifierState |= UInt32(alphaLock >> 8) }

        var deadKeyState: UInt32 = 0
        var chars = [UniChar](repeating: 0, count: 4)
        var length: Int = 0

        let status = UCKeyTranslate(
            unsafeBitCast(keyboardLayout, to: UnsafePointer<UCKeyboardLayout>.self),
            keyCode,
            UInt16(kUCKeyActionDown),
            modifierState,
            UInt32(LMGetKbdType()),
            OptionBits(kUCKeyTranslateNoDeadKeysBit),  // Get actual character, not dead key
            &deadKeyState,
            chars.count,
            &length,
            &chars
        )

        LOG("UCKeyTranslate: keyCode=0x\(String(format: "%02X", keyCode)) mods=0x\(String(format: "%02X", modifierState)) status=\(status) length=\(length)")

        if status == noErr && length > 0 {
            let result = String(utf16CodeUnits: chars, count: length)
            LOG("UCKeyTranslate: -> '\(result)'")
            return result
        }

        // If UCKeyTranslate returned nothing, try common Swedish keyboard mappings
        if flags.contains(.maskAlternate) {
            if let char = swedishOptionMapping(keyCode: keyCode, shift: flags.contains(.maskShift)) {
                LOG("Using Swedish Option mapping: keyCode=0x\(String(format: "%02X", keyCode)) -> '\(char)'")
                return char
            }
        }

        if flags.contains(.maskShift) {
            if let char = swedishShiftMapping(keyCode: keyCode) {
                LOG("Using Swedish Shift mapping: keyCode=0x\(String(format: "%02X", keyCode)) -> '\(char)'")
                return char
            }
        }

        // Try base key mapping (no modifiers or just dead keys)
        if let char = swedishBaseMapping(keyCode: keyCode) {
            LOG("Using Swedish base mapping: keyCode=0x\(String(format: "%02X", keyCode)) -> '\(char)'")
            return char
        }

        return nil
    }

    // Manual mapping for Swedish keyboard combinations that may not be returned by UCKeyTranslate
    private func swedishOptionMapping(keyCode: UInt16, shift: Bool) -> String? {
        // Swedish keyboard Option+key mappings
        // These are for keys that UCKeyTranslate might not handle correctly
        switch keyCode {
        case 0x1E:  // ¨ key (next to Enter) - Option+¨ = ~
            return "~"
        case 0x21:  // Key with å - Option might produce other chars
            return nil
        case 0x27:  // Key with ä
            return nil
        case 0x29:  // Key with ö
            return nil
        case 0x0A:  // § key (left of 1) - Option+§ might produce something
            return nil
        default:
            return nil
        }
    }

    // Manual mapping for Swedish keyboard Shift combinations
    private func swedishShiftMapping(keyCode: UInt16) -> String? {
        switch keyCode {
        case 0x1E:  // ¨ key (next to Enter) - Shift+¨ = ^
            return "^"
        default:
            return nil
        }
    }

    // Manual mapping for Swedish keyboard base keys (no modifiers / dead keys)
    private func swedishBaseMapping(keyCode: UInt16) -> String? {
        switch keyCode {
        case 0x1E:  // ¨ key (next to Enter) - produces ¨ (diaeresis)
            return "¨"
        case 0x18:  // ' key (below ¨) - produces ' (apostrophe)
            return "'"
        default:
            return nil
        }
    }
}

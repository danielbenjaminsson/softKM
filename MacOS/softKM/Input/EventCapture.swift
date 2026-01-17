import Cocoa
import CoreGraphics

class EventCapture {
    static let shared = EventCapture()

    fileprivate var eventTap: CFMachPort?
    private var runLoopSource: CFRunLoopSource?
    private var isRunning = false

    var onKeyEvent: ((CGEvent, Bool) -> Void)?  // event, isKeyDown
    var onMouseMove: ((CGPoint) -> Void)?
    var onMouseButton: ((CGEvent, Bool) -> Void)?  // event, isDown
    var onMouseWheel: ((CGFloat, CGFloat) -> Void)?  // deltaX, deltaY

    private init() {}

    func startCapture() -> Bool {
        guard !isRunning else {
            LOG("Event capture already running")
            return true
        }

        LOG("Starting event capture...")

        // Build event mask in parts to help the compiler
        var eventMask: CGEventMask = 0
        eventMask |= (1 << CGEventType.keyDown.rawValue)
        eventMask |= (1 << CGEventType.keyUp.rawValue)
        eventMask |= (1 << CGEventType.flagsChanged.rawValue)
        eventMask |= (1 << CGEventType.mouseMoved.rawValue)
        eventMask |= (1 << CGEventType.leftMouseDown.rawValue)
        eventMask |= (1 << CGEventType.leftMouseUp.rawValue)
        eventMask |= (1 << CGEventType.rightMouseDown.rawValue)
        eventMask |= (1 << CGEventType.rightMouseUp.rawValue)
        eventMask |= (1 << CGEventType.otherMouseDown.rawValue)
        eventMask |= (1 << CGEventType.otherMouseUp.rawValue)
        eventMask |= (1 << CGEventType.leftMouseDragged.rawValue)
        eventMask |= (1 << CGEventType.rightMouseDragged.rawValue)
        eventMask |= (1 << CGEventType.otherMouseDragged.rawValue)
        eventMask |= (1 << CGEventType.scrollWheel.rawValue)

        guard let tap = CGEvent.tapCreate(
            tap: .cgSessionEventTap,
            place: .headInsertEventTap,
            options: .defaultTap,
            eventsOfInterest: eventMask,
            callback: eventCallback,
            userInfo: Unmanaged.passUnretained(self).toOpaque()
        ) else {
            LOG("Failed to create event tap. Check Accessibility permissions.")
            return false
        }

        eventTap = tap
        runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)

        if let source = runLoopSource {
            CFRunLoopAddSource(CFRunLoopGetCurrent(), source, .commonModes)
            CGEvent.tapEnable(tap: tap, enable: true)
            isRunning = true
            LOG("Event capture started successfully")
            return true
        }

        LOG("Failed to create run loop source")
        return false
    }

    func stopCapture() {
        guard isRunning else { return }

        if let tap = eventTap {
            CGEvent.tapEnable(tap: tap, enable: false)
        }

        if let source = runLoopSource {
            CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, .commonModes)
        }

        eventTap = nil
        runLoopSource = nil
        isRunning = false
    }

    func setEnabled(_ enabled: Bool) {
        if let tap = eventTap {
            CGEvent.tapEnable(tap: tap, enable: enabled)
        }
    }
}

private func eventCallback(
    proxy: CGEventTapProxy,
    type: CGEventType,
    event: CGEvent,
    refcon: UnsafeMutableRawPointer?
) -> Unmanaged<CGEvent>? {
    guard let refcon = refcon else {
        return Unmanaged.passUnretained(event)
    }

    let capture = Unmanaged<EventCapture>.fromOpaque(refcon).takeUnretainedValue()
    let controller = SwitchController.shared

    // If tap is disabled, re-enable it
    if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
        if let tap = capture.eventTap {
            CGEvent.tapEnable(tap: tap, enable: true)
        }
        return Unmanaged.passUnretained(event)
    }

    // Let the switch controller handle the event
    return controller.handleEvent(type: type, event: event)
}

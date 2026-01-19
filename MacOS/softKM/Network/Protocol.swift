import Foundation

enum EventType: UInt8 {
    case keyDown = 0x01
    case keyUp = 0x02
    case mouseMove = 0x03
    case mouseDown = 0x04
    case mouseUp = 0x05
    case mouseWheel = 0x06
    case controlSwitch = 0x10
    case screenInfo = 0x11
    case settingsSync = 0x12
    case teamMonitor = 0x13
    case heartbeat = 0xF0
    case heartbeatAck = 0xF1
}

enum InputEvent {
    case keyDown(keyCode: UInt32, modifiers: UInt32, characters: String)
    case keyUp(keyCode: UInt32, modifiers: UInt32)
    case mouseMove(x: Float, y: Float, relative: Bool, modifiers: UInt32)
    case mouseDown(buttons: UInt32, x: Float, y: Float, modifiers: UInt32, clicks: Int32)
    case mouseUp(buttons: UInt32, x: Float, y: Float, modifiers: UInt32)
    case mouseWheel(deltaX: Float, deltaY: Float, modifiers: UInt32)
    case controlSwitch(toHaiku: Bool, yFromBottom: Float)
    case screenInfo(width: Float, height: Float)
    case settingsSync(edgeDwellTime: Float)  // dwell time in seconds
    case teamMonitor
    case heartbeat
    case heartbeatAck

    var eventType: EventType {
        switch self {
        case .keyDown: return .keyDown
        case .keyUp: return .keyUp
        case .mouseMove: return .mouseMove
        case .mouseDown: return .mouseDown
        case .mouseUp: return .mouseUp
        case .mouseWheel: return .mouseWheel
        case .controlSwitch: return .controlSwitch
        case .screenInfo: return .screenInfo
        case .settingsSync: return .settingsSync
        case .teamMonitor: return .teamMonitor
        case .heartbeat: return .heartbeat
        case .heartbeatAck: return .heartbeatAck
        }
    }
}

struct Protocol {
    static let magic: UInt16 = 0x534B  // "SK"
    static let version: UInt8 = 0x01

    static func encode(_ event: InputEvent) -> Data {
        var data = Data()

        // Header: magic (2) + version (1) + type (1) + length (4)
        var magicLE = magic.littleEndian
        data.append(contentsOf: withUnsafeBytes(of: &magicLE) { Array($0) })
        data.append(version)
        data.append(event.eventType.rawValue)

        let payload = encodePayload(event)
        var lengthLE = UInt32(payload.count).littleEndian
        data.append(contentsOf: withUnsafeBytes(of: &lengthLE) { Array($0) })
        data.append(payload)

        return data
    }

    private static func encodePayload(_ event: InputEvent) -> Data {
        var payload = Data()

        switch event {
        case .keyDown(let keyCode, let modifiers, let characters):
            appendUInt32(&payload, keyCode)
            appendUInt32(&payload, modifiers)
            let bytes = Array(characters.utf8)
            payload.append(UInt8(bytes.count))
            payload.append(contentsOf: bytes)

        case .keyUp(let keyCode, let modifiers):
            appendUInt32(&payload, keyCode)
            appendUInt32(&payload, modifiers)

        case .mouseMove(let x, let y, let relative, let modifiers):
            appendFloat(&payload, x)
            appendFloat(&payload, y)
            payload.append(relative ? 1 : 0)
            appendUInt32(&payload, modifiers)

        case .mouseDown(let buttons, let x, let y, let modifiers, let clicks):
            appendUInt32(&payload, buttons)
            appendFloat(&payload, x)
            appendFloat(&payload, y)
            appendUInt32(&payload, modifiers)
            appendUInt32(&payload, UInt32(clicks))  // Add click count from macOS

        case .mouseUp(let buttons, let x, let y, let modifiers):
            appendUInt32(&payload, buttons)
            appendFloat(&payload, x)
            appendFloat(&payload, y)
            appendUInt32(&payload, modifiers)

        case .mouseWheel(let deltaX, let deltaY, let modifiers):
            appendFloat(&payload, deltaX)
            appendFloat(&payload, deltaY)
            appendUInt32(&payload, modifiers)

        case .controlSwitch(let toHaiku, let yFromBottom):
            payload.append(toHaiku ? 0 : 1)
            appendFloat(&payload, yFromBottom)

        case .screenInfo(let width, let height):
            appendFloat(&payload, width)
            appendFloat(&payload, height)

        case .settingsSync(let edgeDwellTime):
            appendFloat(&payload, edgeDwellTime)

        case .teamMonitor, .heartbeat, .heartbeatAck:
            break
        }

        return payload
    }

    private static func appendUInt32(_ data: inout Data, _ value: UInt32) {
        var valueLE = value.littleEndian
        data.append(contentsOf: withUnsafeBytes(of: &valueLE) { Array($0) })
    }

    private static func appendFloat(_ data: inout Data, _ value: Float) {
        var bits = value.bitPattern.littleEndian
        data.append(contentsOf: withUnsafeBytes(of: &bits) { Array($0) })
    }
}

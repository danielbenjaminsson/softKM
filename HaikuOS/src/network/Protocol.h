#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <SupportDefs.h>

// Protocol constants
#define PROTOCOL_MAGIC      0x534B  // "SK"
#define PROTOCOL_VERSION    0x01

// Event types
enum EventType {
    EVENT_KEY_DOWN      = 0x01,
    EVENT_KEY_UP        = 0x02,
    EVENT_MOUSE_MOVE    = 0x03,
    EVENT_MOUSE_DOWN    = 0x04,
    EVENT_MOUSE_UP      = 0x05,
    EVENT_MOUSE_WHEEL   = 0x06,
    EVENT_CONTROL_SWITCH = 0x10,
    EVENT_SCREEN_INFO   = 0x11,
    EVENT_SETTINGS_SYNC = 0x12,
    EVENT_HEARTBEAT     = 0xF0,
    EVENT_HEARTBEAT_ACK = 0xF1
};

// Protocol header structure
struct ProtocolHeader {
    uint16  magic;
    uint8   version;
    uint8   eventType;
    uint32  length;
} __attribute__((packed));

// Event payload structures
struct KeyEventPayload {
    uint32  keyCode;
    uint32  modifiers;
    uint8   numBytes;
    // followed by UTF-8 bytes
} __attribute__((packed));

struct MouseMovePayload {
    float   x;
    float   y;
    uint8   relative;
    uint32  modifiers;
} __attribute__((packed));

struct MouseButtonPayload {
    uint32  buttons;
    float   x;
    float   y;
    uint32  modifiers;
} __attribute__((packed));

struct MouseDownPayload {
    uint32  buttons;
    float   x;
    float   y;
    uint32  modifiers;
    uint32  clicks;     // Click count from macOS (1=single, 2=double, etc.)
} __attribute__((packed));

struct MouseWheelPayload {
    float   deltaX;
    float   deltaY;
    uint32  modifiers;
} __attribute__((packed));

struct ControlSwitchPayload {
    uint8   direction;  // 0 = toHaiku, 1 = toMac
    float   yFromBottom;  // Y position in pixels from bottom for smooth transition
} __attribute__((packed));

struct ScreenInfoPayload {
    float   width;
    float   height;
} __attribute__((packed));

struct SettingsSyncPayload {
    float   edgeDwellTime;  // dwell time in seconds
} __attribute__((packed));

// Modifier key mapping (macOS -> Haiku)
// macOS:  Shift=0x01, Option=0x02, Control=0x04, Fn=0x10, CapsLock=0x20, Command=0x40
// Haiku generic:  B_SHIFT_KEY=0x01, B_COMMAND_KEY=0x02, B_CONTROL_KEY=0x04,
//                 B_CAPS_LOCK=0x10, B_NUM_LOCK=0x40, B_OPTION_KEY=0x80
inline uint32 MapModifiers(uint32 macModifiers)
{
    uint32 haikuModifiers = 0;

    // Map to generic modifier flags only (not left/right specific)
    if (macModifiers & 0x01) haikuModifiers |= 0x01;   // Shift -> B_SHIFT_KEY
    if (macModifiers & 0x02) haikuModifiers |= 0x80;   // Option -> B_OPTION_KEY
    if (macModifiers & 0x04) haikuModifiers |= 0x04;   // Control -> B_CONTROL_KEY
    if (macModifiers & 0x20) haikuModifiers |= 0x10;   // CapsLock -> B_CAPS_LOCK
    if (macModifiers & 0x40) haikuModifiers |= 0x02;   // Command -> B_COMMAND_KEY

    return haikuModifiers;
}

#endif // PROTOCOL_H

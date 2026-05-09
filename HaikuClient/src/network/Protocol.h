#ifndef PROTOCOL_H
#define PROTOCOL_H

// Shared protocol definitions — identical to HaikuOS/src/network/Protocol.h
// so both sides stay in sync.

#include <SupportDefs.h>

#define PROTOCOL_MAGIC      0x534B   // "SK"
#define PROTOCOL_VERSION    0x01

enum EventType {
    EVENT_KEY_DOWN       = 0x01,
    EVENT_KEY_UP         = 0x02,
    EVENT_MOUSE_MOVE     = 0x03,
    EVENT_MOUSE_DOWN     = 0x04,
    EVENT_MOUSE_UP       = 0x05,
    EVENT_MOUSE_WHEEL    = 0x06,
    EVENT_CONTROL_SWITCH = 0x10,
    EVENT_SCREEN_INFO    = 0x11,
    EVENT_SETTINGS_SYNC  = 0x12,
    EVENT_TEAM_MONITOR   = 0x13,
    EVENT_CLIPBOARD_SYNC = 0x14,
    EVENT_HEARTBEAT      = 0xF0,
    EVENT_HEARTBEAT_ACK  = 0xF1
};

struct ProtocolHeader {
    uint16  magic;
    uint8   version;
    uint8   eventType;
    uint32  length;
} __attribute__((packed));

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
    uint32  clicks;
} __attribute__((packed));

struct MouseWheelPayload {
    float   deltaX;
    float   deltaY;
    uint32  modifiers;
} __attribute__((packed));

struct ControlSwitchPayload {
    uint8   direction;   // 0 = toRight, 1 = toLeft
    float   yRatio;      // 0.0 = top, 1.0 = bottom
} __attribute__((packed));

struct ScreenInfoPayload {
    float   width;
    float   height;
    // Optional trailing byte added in protocol r2: sender platform
    // (0 = macOS, 1 = Haiku). Receivers MUST check header->length to
    // decide whether this byte is present; older clients (~1.0.x)
    // send a payload of exactly 8 bytes with no platform byte, in
    // which case the receiver should default senderPlatform = 0
    // (macOS) for backwards compatibility.
    uint8   senderPlatform;
} __attribute__((packed));

// senderPlatform values
enum SenderPlatform {
    SENDER_MAC   = 0,
    SENDER_HAIKU = 1
};

struct SettingsSyncPayload {
    float   edgeDwellTime;
    uint8   macSwitchEdge;      // repurposed: left Haiku switch edge
    uint8   haikuReturnEdge;    // right Haiku return edge
    float   yOffsetRatio;
} __attribute__((packed));

struct ClipboardSyncPayload {
    uint8   contentType;
    uint32  dataLength;
} __attribute__((packed));

enum SwitchEdge {
    EDGE_RIGHT  = 0,
    EDGE_LEFT   = 1,
    EDGE_TOP    = 2,
    EDGE_BOTTOM = 3
};

// Haiku modifier flags (used natively — no translation needed Haiku→Haiku)
// B_SHIFT_KEY=0x01, B_COMMAND_KEY=0x02, B_CONTROL_KEY=0x04,
// B_CAPS_LOCK=0x08, B_SCROLL_LOCK=0x10, B_NUM_LOCK=0x20,
// B_OPTION_KEY=0x40, B_MENU_KEY=0x80
// Left-specific: B_LEFT_SHIFT_KEY=0x1000, B_LEFT_COMMAND_KEY=0x4000,
//                B_LEFT_CONTROL_KEY=0x10000, B_LEFT_OPTION_KEY=0x40000

#endif // PROTOCOL_H

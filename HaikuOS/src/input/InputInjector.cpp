#include "InputInjector.h"
#include "../network/NetworkServer.h"
#include "../Logger.h"

#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <Screen.h>
#include <InterfaceDefs.h>
#include <game/WindowScreen.h>
#include <OS.h>

#include <cstring>
#include <cstdio>
#include <cmath>

// Message codes for communication with input_server add-on
enum {
    SOFTKM_INJECT_MOUSE_DOWN = 'sMdn',
    SOFTKM_INJECT_MOUSE_UP = 'sMup',
    SOFTKM_INJECT_MOUSE_MOVE = 'sMmv',
    SOFTKM_INJECT_MOUSE_WHEEL = 'sMwh',
    SOFTKM_INJECT_KEY_DOWN = 'sKdn',
    SOFTKM_INJECT_KEY_UP = 'sKup',
};

// macOS to Haiku keycode mapping table
// macOS virtual key codes -> Haiku key codes
static const struct KeyMapping {
    uint32 macKey;
    uint32 haikuKey;
} kKeyMap[] = {
    // Letters
    { 0x00, 0x3c },  // A
    { 0x01, 0x50 },  // S
    { 0x02, 0x3e },  // D
    { 0x03, 0x3d },  // F
    { 0x04, 0x4d },  // H
    { 0x05, 0x3f },  // G
    { 0x06, 0x4f },  // Z
    { 0x07, 0x51 },  // X
    { 0x08, 0x52 },  // C
    { 0x09, 0x4e },  // V
    { 0x0B, 0x40 },  // B
    { 0x0C, 0x29 },  // Q
    { 0x0D, 0x2a },  // W
    { 0x0E, 0x2b },  // E
    { 0x0F, 0x2c },  // R
    { 0x10, 0x2e },  // Y
    { 0x11, 0x2d },  // T
    { 0x12, 0x12 },  // 1
    { 0x13, 0x13 },  // 2
    { 0x14, 0x14 },  // 3
    { 0x15, 0x15 },  // 4
    { 0x17, 0x16 },  // 5
    { 0x16, 0x17 },  // 6
    { 0x1A, 0x18 },  // 7
    { 0x1C, 0x19 },  // 8
    { 0x19, 0x1a },  // 9
    { 0x1D, 0x1b },  // 0
    { 0x1B, 0x1c },  // -
    { 0x18, 0x1d },  // =
    { 0x1E, 0x46 },  // ]
    { 0x1F, 0x41 },  // O
    { 0x20, 0x2f },  // U
    { 0x21, 0x45 },  // [
    { 0x22, 0x30 },  // I
    { 0x23, 0x42 },  // P
    { 0x25, 0x53 },  // L
    { 0x26, 0x31 },  // J
    { 0x27, 0x54 },  // '
    { 0x28, 0x43 },  // K
    { 0x29, 0x55 },  // ;
    { 0x2A, 0x47 },  // backslash
    { 0x2B, 0x56 },  // ,
    { 0x2C, 0x57 },  // /
    { 0x2D, 0x44 },  // N
    { 0x2E, 0x58 },  // M
    { 0x2F, 0x59 },  // .
    { 0x32, 0x11 },  // `

    // Special keys
    { 0x24, 0x47 },  // Return
    { 0x30, 0x26 },  // Tab
    { 0x31, 0x5e },  // Space
    { 0x33, 0x1e },  // Backspace
    { 0x35, 0x01 },  // Escape
    { 0x36, 0x5f },  // Right Command -> Right Alt (B_COMMAND_KEY)
    { 0x37, 0x5d },  // Left Command -> Left Alt (B_COMMAND_KEY)
    { 0x38, 0x4b },  // Left Shift
    { 0x39, 0x3b },  // Caps Lock
    { 0x3A, 0x66 },  // Left Option -> Left Win (B_OPTION_KEY)
    { 0x3B, 0x5c },  // Left Control
    { 0x3C, 0x56 },  // Right Shift
    { 0x3D, 0x67 },  // Right Option -> Right Win (B_OPTION_KEY)
    { 0x3E, 0x60 },  // Right Control
    { 0x3F, 0x68 },  // Function

    // Function keys
    { 0x7A, 0x02 },  // F1
    { 0x78, 0x03 },  // F2
    { 0x63, 0x04 },  // F3
    { 0x76, 0x05 },  // F4
    { 0x60, 0x06 },  // F5
    { 0x61, 0x07 },  // F6
    { 0x62, 0x08 },  // F7
    { 0x64, 0x09 },  // F8
    { 0x65, 0x0a },  // F9
    { 0x6D, 0x0b },  // F10
    { 0x67, 0x0c },  // F11
    { 0x6F, 0x0d },  // F12

    // Arrow keys (using dedicated keycodes to avoid collisions)
    { 0x7B, 0x61 },  // Left Arrow
    { 0x7C, 0x63 },  // Right Arrow
    { 0x7D, 0x62 },  // Down Arrow (was 0x57, collided with /)
    { 0x7E, 0x9e },  // Up Arrow (was 0x38, collided with Numpad 8)

    // Navigation keys
    { 0x73, 0x20 },  // Home
    { 0x77, 0x35 },  // End
    { 0x74, 0x21 },  // Page Up
    { 0x79, 0x36 },  // Page Down
    { 0x75, 0x34 },  // Delete (forward delete)

    // Numpad
    { 0x52, 0x64 },  // Numpad 0
    { 0x53, 0x58 },  // Numpad 1
    { 0x54, 0x59 },  // Numpad 2
    { 0x55, 0x5a },  // Numpad 3
    { 0x56, 0x48 },  // Numpad 4
    { 0x57, 0x49 },  // Numpad 5
    { 0x58, 0x4a },  // Numpad 6
    { 0x59, 0x37 },  // Numpad 7
    { 0x5B, 0x38 },  // Numpad 8
    { 0x5C, 0x39 },  // Numpad 9
    { 0x41, 0x65 },  // Numpad .
    { 0x43, 0x24 },  // Numpad *
    { 0x45, 0x3a },  // Numpad +
    { 0x47, 0x22 },  // Numpad Clear
    { 0x4B, 0x25 },  // Numpad /
    { 0x4C, 0x5b },  // Numpad Enter
    { 0x4E, 0x25 },  // Numpad -
};

static const size_t kKeyMapSize = sizeof(kKeyMap) / sizeof(kKeyMap[0]);

InputInjector::InputInjector()
    : fMousePosition(0, 0),
      fCurrentButtons(0),
      fCurrentModifiers(0),
      fActive(false),
      fKeyboardPort(-1),
      fMousePort(-1),
      fNetworkServer(nullptr),
      fEdgeDwellStart(0),
      fDwellTime(300000),  // default 300ms
      fAtLeftEdge(false),
      fLastClickTime(0),
      fLastClickPosition(0, 0),
      fClickCount(0),
      fLastClickButtons(0)
{
    // Initialize mouse position to center of screen
    BScreen screen;
    BRect frame = screen.Frame();
    fMousePosition.Set(frame.Width() / 2, frame.Height() / 2);

    // Try to find the addon ports
    fKeyboardPort = FindKeyboardPort();
    if (fKeyboardPort >= 0) {
        LOG("Found keyboard addon port: %ld", fKeyboardPort);
    } else {
        LOG("Keyboard addon not found - keys won't work");
    }

    fMousePort = FindMousePort();
    if (fMousePort >= 0) {
        LOG("Found mouse addon port: %ld", fMousePort);
    } else {
        LOG("Mouse addon not found - clicks/scroll won't work");
    }
}

port_id InputInjector::FindKeyboardPort()
{
    return find_port("softKM_keyboard_port");
}

port_id InputInjector::FindMousePort()
{
    return find_port("softKM_mouse_port");
}

bool InputInjector::SendToKeyboardAddon(BMessage* msg)
{
    port_info info;
    if (fKeyboardPort < 0 || get_port_info(fKeyboardPort, &info) != B_OK) {
        fKeyboardPort = FindKeyboardPort();
        if (fKeyboardPort < 0) {
            LOG("Keyboard addon port not found");
            return false;
        }
        LOG("Re-acquired keyboard addon port: %ld", fKeyboardPort);
    }

    ssize_t flatSize = msg->FlattenedSize();
    char* buffer = new char[flatSize];

    if (msg->Flatten(buffer, flatSize) == B_OK) {
        status_t result = write_port_etc(fKeyboardPort, msg->what, buffer, flatSize,
                                          B_RELATIVE_TIMEOUT, 100000);
        delete[] buffer;
        if (result != B_OK) {
            LOG("write_port (keyboard) failed: %s", strerror(result));
            fKeyboardPort = -1;
            return false;
        }
        return true;
    }

    delete[] buffer;
    LOG("Failed to flatten keyboard message");
    return false;
}

bool InputInjector::SendToMouseAddon(BMessage* msg)
{
    port_info info;
    if (fMousePort < 0 || get_port_info(fMousePort, &info) != B_OK) {
        fMousePort = FindMousePort();
        if (fMousePort < 0) {
            LOG("Mouse addon port not found");
            return false;
        }
        LOG("Re-acquired mouse addon port: %ld", fMousePort);
    }

    ssize_t flatSize = msg->FlattenedSize();
    char* buffer = new char[flatSize];

    if (msg->Flatten(buffer, flatSize) == B_OK) {
        status_t result = write_port_etc(fMousePort, msg->what, buffer, flatSize,
                                          B_RELATIVE_TIMEOUT, 100000);
        delete[] buffer;
        if (result != B_OK) {
            LOG("write_port (mouse) failed: %s", strerror(result));
            fMousePort = -1;
            return false;
        }
        return true;
    }

    delete[] buffer;
    LOG("Failed to flatten mouse message");
    return false;
}

InputInjector::~InputInjector()
{
}

void InputInjector::SetActive(bool active, float yFromBottom)
{
    if (fActive != active) {
        fActive = active;
        LOG("Input injection %s", active ? "ACTIVATED" : "DEACTIVATED");

        if (active) {
            // Position mouse near left edge (where user is coming from)
            // but not too close to trigger immediate switch back (50px from edge)
            // Use yFromBottom for smooth vertical transition (bottom-aligned monitors)
            BScreen screen;
            BRect frame = screen.Frame();
            float screenHeight = frame.Height() + 1;  // BRect Height() returns h-1
            float startX = 50.0f;  // 50 pixels from left edge

            // Haiku Y is top-down, so convert from bottom-up:
            // Haiku Y = (screenHeight - 1) - yFromBottom
            // But we also need to clamp yFromBottom to our screen height first
            if (yFromBottom >= screenHeight) {
                yFromBottom = screenHeight - 1;  // Clamp to top
            }
            float startY = (screenHeight - 1) - yFromBottom;

            // Clamp to screen bounds (shouldn't be needed but safety)
            if (startY < 0) startY = 0;
            if (startY > screenHeight - 1) startY = screenHeight - 1;

            fMousePosition.Set(startX, startY);
            set_mouse_position((int32)fMousePosition.x, (int32)fMousePosition.y);
            LOG("MAC→HAIKU: yFromBottom=%.0f screenHeight=%.0f → startY=%.0f",
                yFromBottom, screenHeight, startY);

            // Reset edge detection state
            fAtLeftEdge = false;
            fEdgeDwellStart = 0;
        }
    }
}

uint32 InputInjector::TranslateKeyCode(uint32 macKeyCode)
{
    for (size_t i = 0; i < kKeyMapSize; i++) {
        if (kKeyMap[i].macKey == macKeyCode) {
            return kKeyMap[i].haikuKey;
        }
    }

    // Return the original code if no mapping found
    LOG("Unknown macOS keycode: 0x%02X", macKeyCode);
    return macKeyCode;
}

void InputInjector::UpdateMousePosition(float x, float y, bool relative)
{
    BScreen screen;
    BRect frame = screen.Frame();

    if (relative) {
        fMousePosition.x += x;
        fMousePosition.y += y;
    } else {
        fMousePosition.x = x;
        fMousePosition.y = y;
    }

    // Clamp to screen bounds
    if (fMousePosition.x < 0) fMousePosition.x = 0;
    if (fMousePosition.y < 0) fMousePosition.y = 0;
    if (fMousePosition.x > frame.Width()) fMousePosition.x = frame.Width();
    if (fMousePosition.y > frame.Height()) fMousePosition.y = frame.Height();
}

void InputInjector::InjectKeyDown(uint32 keyCode, uint32 modifiers,
    const char* bytes, uint8 numBytes)
{
    if (!fActive) {
        LOG("KeyDown ignored (not active)");
        return;
    }

    uint32 haikuKey = TranslateKeyCode(keyCode);
    fCurrentModifiers = modifiers;

    // Log bytes for debugging
    char bytesHex[64] = {0};
    for (int i = 0; i < numBytes && i < 10; i++) {
        snprintf(bytesHex + i*3, 4, "%02X ", (uint8)bytes[i]);
    }
    LOG("KeyDown: mac=0x%02X haiku=0x%02X mods=0x%02X numBytes=%d bytes=[%s]",
        keyCode, haikuKey, modifiers, numBytes, bytesHex);

    BMessage msg(SOFTKM_INJECT_KEY_DOWN);
    msg.AddInt32("key", haikuKey);
    msg.AddInt32("modifiers", modifiers);

    // Add raw character
    if (numBytes > 0 && bytes != nullptr) {
        msg.AddInt32("raw_char", (int32)(uint8)bytes[0]);
        // Add null-terminated string
        char str[16];
        size_t len = numBytes < sizeof(str) - 1 ? numBytes : sizeof(str) - 1;
        memcpy(str, bytes, len);
        str[len] = '\0';
        msg.AddString("bytes", str);
        LOG("  -> Sending raw_char=0x%02X bytes=[0x%02X]", (uint8)bytes[0], (uint8)str[0]);
    } else {
        msg.AddInt32("raw_char", 0);
        msg.AddString("bytes", "");
        LOG("  -> No bytes to send");
    }

    // Send through keyboard add-on
    if (!SendToKeyboardAddon(&msg)) {
        LOG("Failed to send KeyDown to addon");
    }
}

void InputInjector::InjectKeyUp(uint32 keyCode, uint32 modifiers)
{
    if (!fActive)
        return;

    uint32 haikuKey = TranslateKeyCode(keyCode);
    fCurrentModifiers = modifiers;

    BMessage msg(SOFTKM_INJECT_KEY_UP);
    msg.AddInt32("key", haikuKey);
    msg.AddInt32("modifiers", modifiers);

    // Send through keyboard add-on
    if (!SendToKeyboardAddon(&msg)) {
        LOG("Failed to send KeyUp to addon");
    }
}

void InputInjector::InjectMouseMove(float x, float y, bool relative, uint32 modifiers)
{
    if (!fActive)
        return;

    fCurrentModifiers = modifiers;
    UpdateMousePosition(x, y, relative);
    LOG("MouseMove: rel=%d pos=(%.1f,%.1f) mods=0x%08X",
        relative, fMousePosition.x, fMousePosition.y, modifiers);

    // Send through mouse addon so B_MOUSE_MOVED includes modifiers
    // (needed for Stack and Tile window grouping with Option key)
    BMessage msg(SOFTKM_INJECT_MOUSE_MOVE);
    msg.AddPoint("where", fMousePosition);
    msg.AddInt32("buttons", fCurrentButtons);
    msg.AddInt32("modifiers", modifiers);
    SendToMouseAddon(&msg);

    // Also update cursor position directly for immediate feedback
    set_mouse_position((int32)fMousePosition.x, (int32)fMousePosition.y);

    // Edge detection for switching back to macOS
    const float kEdgeThreshold = 5.0f;

    if (fMousePosition.x <= kEdgeThreshold) {
        if (!fAtLeftEdge) {
            // Just entered left edge
            fAtLeftEdge = true;
            fEdgeDwellStart = system_time();
            LOG("Entered left edge - starting dwell timer (%.1fs)", fDwellTime / 1000000.0f);
        } else {
            // Still at left edge - check dwell time
            bigtime_t dwellTime = system_time() - fEdgeDwellStart;
            if (dwellTime >= fDwellTime && fNetworkServer != nullptr) {
                LOG("Left edge dwell complete - switching to macOS");
                // Calculate Y from bottom for macOS
                BScreen screen;
                BRect frame = screen.Frame();
                float screenHeight = frame.Height() + 1;
                float yFromBottom = (screenHeight - 1) - fMousePosition.y;
                LOG("HAIKU→MAC: mouseY=%.0f screenHeight=%.0f → yFromBottom=%.0f",
                    fMousePosition.y, screenHeight, yFromBottom);
                fNetworkServer->SendControlSwitch(1, yFromBottom);  // 1 = toMac
                fAtLeftEdge = false;
                fActive = false;
            }
        }
    } else {
        // Not at left edge - reset
        if (fAtLeftEdge) {
            LOG("Left edge - dwell cancelled");
        }
        fAtLeftEdge = false;
        fEdgeDwellStart = 0;
    }
}

void InputInjector::InjectMouseDown(uint32 buttons, float x, float y, uint32 modifiers, uint32 clicks)
{
    if (!fActive)
        return;

    fCurrentButtons |= buttons;
    fCurrentModifiers = modifiers;
    bigtime_t now = system_time();

    // Let the addon handle click tracking - it has better timing info
    LOG("MouseDown: buttons=0x%02X mods=0x%02X at (%.1f,%.1f)",
        fCurrentButtons, modifiers, fMousePosition.x, fMousePosition.y);

    // Don't call set_mouse_position() here - it conflicts with event pipeline
    // The addon sends a B_MOUSE_MOVED before each click to sync position

    BMessage msg(SOFTKM_INJECT_MOUSE_DOWN);
    msg.AddInt64("when", now);
    msg.AddPoint("where", fMousePosition);
    msg.AddInt32("buttons", fCurrentButtons);
    msg.AddInt32("modifiers", modifiers);
    msg.AddInt32("clicks", clicks);  // Use macOS click count directly

    if (SendToMouseAddon(&msg)) {
        LOG("MouseDown sent to addon successfully");
    } else {
        LOG("Failed to send MouseDown to addon");
    }
}

void InputInjector::InjectMouseUp(uint32 buttons, float x, float y, uint32 modifiers)
{
    if (!fActive)
        return;

    fCurrentButtons &= ~buttons;
    fCurrentModifiers = modifiers;
    LOG("MouseUp: buttons=0x%02X at (%.1f,%.1f)", fCurrentButtons,
        fMousePosition.x, fMousePosition.y);

    BMessage msg(SOFTKM_INJECT_MOUSE_UP);
    msg.AddInt64("when", system_time());
    msg.AddPoint("where", fMousePosition);
    msg.AddInt32("buttons", fCurrentButtons);
    msg.AddInt32("modifiers", modifiers);

    if (!SendToMouseAddon(&msg)) {
        LOG("Failed to send MouseUp to addon");
    }
}

void InputInjector::InjectMouseWheel(float deltaX, float deltaY, uint32 modifiers)
{
    if (!fActive)
        return;

    fCurrentModifiers = modifiers;
    LOG("MouseWheel: delta=(%.2f,%.2f)", deltaX, deltaY);

    BMessage msg(SOFTKM_INJECT_MOUSE_WHEEL);
    msg.AddInt64("when", system_time());
    msg.AddFloat("delta_x", deltaX);
    msg.AddFloat("delta_y", deltaY);
    msg.AddInt32("modifiers", modifiers);

    if (!SendToMouseAddon(&msg)) {
        LOG("Failed to send MouseWheel to addon");
    }
}

void InputInjector::InjectTeamMonitor()
{
    // Inject Ctrl+Alt+Delete to trigger Haiku's built-in Team Monitor
    // This works regardless of whether softKM is active
    LOG("Injecting Ctrl+Alt+Delete for Team Monitor");

    // Haiku key codes
    const uint32 kCtrlKey = 0x5c;    // Left Control
    const uint32 kAltKey = 0x5d;     // Left Alt (Command)
    const uint32 kDeleteKey = 0x34;  // Delete (forward delete)

    // Modifiers for Ctrl+Alt
    const uint32 kCtrlMod = 0x04 | 0x10000;    // B_CONTROL_KEY | B_LEFT_CONTROL_KEY
    const uint32 kAltMod = 0x02 | 0x4000;      // B_COMMAND_KEY | B_LEFT_COMMAND_KEY
    const uint32 kCtrlAltMod = kCtrlMod | kAltMod;

    // Press Ctrl
    BMessage ctrlDown(SOFTKM_INJECT_KEY_DOWN);
    ctrlDown.AddInt32("key", kCtrlKey);
    ctrlDown.AddInt32("modifiers", kCtrlMod);
    ctrlDown.AddInt32("raw_char", 0);
    ctrlDown.AddString("bytes", "");
    SendToKeyboardAddon(&ctrlDown);

    // Press Alt
    BMessage altDown(SOFTKM_INJECT_KEY_DOWN);
    altDown.AddInt32("key", kAltKey);
    altDown.AddInt32("modifiers", kCtrlAltMod);
    altDown.AddInt32("raw_char", 0);
    altDown.AddString("bytes", "");
    SendToKeyboardAddon(&altDown);

    // Press Delete
    BMessage delDown(SOFTKM_INJECT_KEY_DOWN);
    delDown.AddInt32("key", kDeleteKey);
    delDown.AddInt32("modifiers", kCtrlAltMod);
    delDown.AddInt32("raw_char", 0x7f);  // DEL character
    delDown.AddString("bytes", "\x7f");
    SendToKeyboardAddon(&delDown);

    // Small delay
    snooze(50000);  // 50ms

    // Release Delete
    BMessage delUp(SOFTKM_INJECT_KEY_UP);
    delUp.AddInt32("key", kDeleteKey);
    delUp.AddInt32("modifiers", kCtrlAltMod);
    SendToKeyboardAddon(&delUp);

    // Release Alt
    BMessage altUp(SOFTKM_INJECT_KEY_UP);
    altUp.AddInt32("key", kAltKey);
    altUp.AddInt32("modifiers", kCtrlMod);
    SendToKeyboardAddon(&altUp);

    // Release Ctrl
    BMessage ctrlUp(SOFTKM_INJECT_KEY_UP);
    ctrlUp.AddInt32("key", kCtrlKey);
    ctrlUp.AddInt32("modifiers", 0);
    SendToKeyboardAddon(&ctrlUp);
}

void InputInjector::ProcessEvent(BMessage* message)
{
    // Process events from BMessage (for future use)
    // Currently, events are processed directly from the network server
}

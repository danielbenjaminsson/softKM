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
    { 0x12, 0x13 },  // 1
    { 0x13, 0x14 },  // 2
    { 0x14, 0x15 },  // 3
    { 0x15, 0x16 },  // 4
    { 0x16, 0x18 },  // 6
    { 0x17, 0x17 },  // 5
    { 0x18, 0x1a },  // =
    { 0x19, 0x1b },  // 9
    { 0x1A, 0x19 },  // 7
    { 0x1B, 0x1c },  // -
    { 0x1C, 0x1d },  // 8
    { 0x1D, 0x1e },  // 0
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
    { 0x36, 0x5d },  // Right Command -> Right Option
    { 0x37, 0x5c },  // Left Command
    { 0x38, 0x4b },  // Left Shift
    { 0x39, 0x3b },  // Caps Lock
    { 0x3A, 0x5d },  // Left Option
    { 0x3B, 0x5c },  // Left Control
    { 0x3C, 0x56 },  // Right Shift
    { 0x3D, 0x60 },  // Right Option
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

    // Arrow keys
    { 0x7B, 0x61 },  // Left Arrow
    { 0x7C, 0x63 },  // Right Arrow
    { 0x7D, 0x57 },  // Down Arrow
    { 0x7E, 0x38 },  // Up Arrow

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
      fAddonPort(-1),
      fNetworkServer(nullptr),
      fEdgeDwellStart(0),
      fAtLeftEdge(false)
{
    // Initialize mouse position to center of screen
    BScreen screen;
    BRect frame = screen.Frame();
    fMousePosition.Set(frame.Width() / 2, frame.Height() / 2);

    // Try to find the addon port
    fAddonPort = FindAddonPort();
    if (fAddonPort >= 0) {
        LOG("Found input addon port: %ld", fAddonPort);
    } else {
        LOG("Input addon not found - clicks/scroll won't work");
    }
}

port_id InputInjector::FindAddonPort()
{
    return find_port("softKM_input_port");
}

bool InputInjector::SendToAddon(BMessage* msg)
{
    if (fAddonPort < 0) {
        // Try to find port again (addon might have started later)
        fAddonPort = FindAddonPort();
        if (fAddonPort < 0)
            return false;
    }

    ssize_t flatSize = msg->FlattenedSize();
    char* buffer = new char[flatSize];

    if (msg->Flatten(buffer, flatSize) == B_OK) {
        status_t result = write_port(fAddonPort, msg->what, buffer, flatSize);
        delete[] buffer;
        return result == B_OK;
    }

    delete[] buffer;
    return false;
}

InputInjector::~InputInjector()
{
}

void InputInjector::SetActive(bool active)
{
    if (fActive != active) {
        fActive = active;
        LOG("Input injection %s", active ? "ACTIVATED" : "DEACTIVATED");
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
    LOG("KeyDown: mac=0x%02X haiku=0x%02X mods=0x%02X",
        keyCode, haikuKey, modifiers);

    BMessage msg(B_KEY_DOWN);
    msg.AddInt64("when", system_time());
    msg.AddInt32("key", haikuKey);
    msg.AddInt32("modifiers", modifiers);

    // Add raw character
    if (numBytes > 0 && bytes != nullptr) {
        msg.AddInt32("raw_char", (int32)(uint8)bytes[0]);
        for (int i = 0; i < numBytes; i++) {
            msg.AddInt8("byte", bytes[i]);
        }
        // Add null-terminated string
        char str[16];
        size_t len = numBytes < sizeof(str) - 1 ? numBytes : sizeof(str) - 1;
        memcpy(str, bytes, len);
        str[len] = '\0';
        msg.AddString("bytes", str);
    } else {
        msg.AddInt32("raw_char", 0);
        msg.AddString("bytes", "");
    }

    // Send to the active window via input server
    // We use set_mouse_position as a side effect to find the window
    // and then send key events to the app_server
    BMessenger inputServer("application/x-vnd.Be-input_server");
    if (inputServer.IsValid()) {
        inputServer.SendMessage(&msg);
    }
}

void InputInjector::InjectKeyUp(uint32 keyCode, uint32 modifiers)
{
    if (!fActive)
        return;

    uint32 haikuKey = TranslateKeyCode(keyCode);
    fCurrentModifiers = modifiers;

    BMessage msg(B_KEY_UP);
    msg.AddInt64("when", system_time());
    msg.AddInt32("key", haikuKey);
    msg.AddInt32("modifiers", modifiers);
    msg.AddInt32("raw_char", 0);
    msg.AddString("bytes", "");

    BMessenger inputServer("application/x-vnd.Be-input_server");
    if (inputServer.IsValid()) {
        inputServer.SendMessage(&msg);
    }
}

void InputInjector::InjectMouseMove(float x, float y, bool relative)
{
    if (!fActive)
        return;

    UpdateMousePosition(x, y, relative);
    LOG("MouseMove: rel=%d pos=(%.1f,%.1f)",
        relative, fMousePosition.x, fMousePosition.y);

    // Use set_mouse_position to actually move the cursor
    set_mouse_position((int32)fMousePosition.x, (int32)fMousePosition.y);

    // Edge detection for switching back to macOS
    const float kEdgeThreshold = 5.0f;
    const bigtime_t kDwellTime = 300000;  // 300ms in microseconds

    if (fMousePosition.x <= kEdgeThreshold) {
        if (!fAtLeftEdge) {
            // Just entered left edge
            fAtLeftEdge = true;
            fEdgeDwellStart = system_time();
            LOG("Entered left edge - starting dwell timer");
        } else {
            // Still at left edge - check dwell time
            bigtime_t dwellTime = system_time() - fEdgeDwellStart;
            if (dwellTime >= kDwellTime && fNetworkServer != nullptr) {
                LOG("Left edge dwell complete - switching to macOS");
                fNetworkServer->SendControlSwitch(1);  // 1 = toMac
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

void InputInjector::InjectMouseDown(uint32 buttons, float x, float y)
{
    if (!fActive)
        return;

    fCurrentButtons |= buttons;
    LOG("MouseDown: buttons=0x%02X at (%.1f,%.1f)", fCurrentButtons,
        fMousePosition.x, fMousePosition.y);

    BMessage msg(SOFTKM_INJECT_MOUSE_DOWN);
    msg.AddPoint("where", fMousePosition);
    msg.AddInt32("buttons", fCurrentButtons);
    msg.AddInt32("clicks", 1);

    if (!SendToAddon(&msg)) {
        LOG("Failed to send MouseDown to addon");
    }
}

void InputInjector::InjectMouseUp(uint32 buttons, float x, float y)
{
    if (!fActive)
        return;

    fCurrentButtons &= ~buttons;
    LOG("MouseUp: buttons=0x%02X at (%.1f,%.1f)", fCurrentButtons,
        fMousePosition.x, fMousePosition.y);

    BMessage msg(SOFTKM_INJECT_MOUSE_UP);
    msg.AddPoint("where", fMousePosition);
    msg.AddInt32("buttons", fCurrentButtons);

    if (!SendToAddon(&msg)) {
        LOG("Failed to send MouseUp to addon");
    }
}

void InputInjector::InjectMouseWheel(float deltaX, float deltaY)
{
    if (!fActive)
        return;

    LOG("MouseWheel: delta=(%.2f,%.2f)", deltaX, deltaY);

    BMessage msg(SOFTKM_INJECT_MOUSE_WHEEL);
    msg.AddFloat("delta_x", deltaX);
    msg.AddFloat("delta_y", deltaY);

    if (!SendToAddon(&msg)) {
        LOG("Failed to send MouseWheel to addon");
    }
}

void InputInjector::ProcessEvent(BMessage* message)
{
    // Process events from BMessage (for future use)
    // Currently, events are processed directly from the network server
}

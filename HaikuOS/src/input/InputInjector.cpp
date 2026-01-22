#include "InputInjector.h"
#include "../network/NetworkServer.h"
#include "../network/Protocol.h"
#include "../Logger.h"
#include "../settings/Settings.h"
#include "../ui/TeamMonitorWindow.h"

#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <Roster.h>
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
      fAtReturnEdge(false),
      fReturnEdge(EDGE_LEFT),  // default: left edge returns to Mac
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

void InputInjector::SetActive(bool active, float yRatio)
{
    if (fActive != active) {
        fActive = active;
        LOG("Input injection %s", active ? "ACTIVATED" : "DEACTIVATED");

        if (active) {
            // Position mouse near the return edge (where user is coming from)
            // but not too close to trigger immediate switch back (50px from edge)
            // Use yRatio for smooth vertical transition (0.0 = top, 1.0 = bottom)
            BScreen screen;
            BRect frame = screen.Frame();
            float screenWidth = frame.Width() + 1;
            float screenHeight = frame.Height() + 1;  // BRect Height() returns h-1

            float startX, startY;
            const float kEdgeOffset = 50.0f;  // 50 pixels from edge

            // yRatio is 0.0 = top, 1.0 = bottom - matches Haiku's coordinate system
            // Clamp to 0.0-1.0
            if (yRatio < 0.0f) yRatio = 0.0f;
            if (yRatio > 1.0f) yRatio = 1.0f;
            float convertedY = yRatio * (screenHeight - 1);

            // Position based on return edge (opposite of where Mac is)
            switch (fReturnEdge) {
                case EDGE_LEFT:
                    // Mac is on right, user enters from right, position near left edge
                    startX = kEdgeOffset;
                    startY = convertedY;
                    break;
                case EDGE_RIGHT:
                    // Mac is on left, user enters from left, position near right edge
                    startX = screenWidth - kEdgeOffset;
                    startY = convertedY;
                    break;
                case EDGE_TOP:
                    // Mac is below, user enters from bottom, position near top edge
                    startX = screenWidth / 2;
                    startY = kEdgeOffset;
                    break;
                case EDGE_BOTTOM:
                    // Mac is above, user enters from top, position near bottom edge
                    startX = screenWidth / 2;
                    startY = screenHeight - kEdgeOffset;
                    break;
                default:
                    startX = kEdgeOffset;
                    startY = convertedY;
                    break;
            }

            // Clamp to screen bounds
            if (startX < 0) startX = 0;
            if (startX > screenWidth - 1) startX = screenWidth - 1;
            if (startY < 0) startY = 0;
            if (startY > screenHeight - 1) startY = screenHeight - 1;

            fMousePosition.Set(startX, startY);
            set_mouse_position((int32)fMousePosition.x, (int32)fMousePosition.y);
            LOG("MAC→HAIKU: yRatio=%.2f returnEdge=%d → pos=(%.0f,%.0f)",
                yRatio, fReturnEdge, startX, startY);

            // Reset edge detection state
            fAtReturnEdge = false;
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
    // Note: Removed per-key logging for performance

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
    } else {
        msg.AddInt32("raw_char", 0);
        msg.AddString("bytes", "");
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
    bool gameMode = Settings::GetGameMode();

    BPoint positionToSend;

    if (gameMode && relative) {
        // Game mode: SDL games expect delta from window center
        // Always send screen_center + delta, let SDL handle cursor
        BScreen screen;
        BRect frame = screen.Frame();
        float centerX = frame.Width() / 2;
        float centerY = frame.Height() / 2;
        positionToSend.Set(centerX + x, centerY + y);
        // Don't update fMousePosition or system cursor - SDL manages everything
    } else {
        // Normal mode: track absolute position
        UpdateMousePosition(x, y, relative);
        positionToSend = fMousePosition;

        // Update system cursor position
        if (fCurrentButtons == 0) {
            set_mouse_position((int32)fMousePosition.x, (int32)fMousePosition.y);
        }
    }

    // Send B_MOUSE_MOVED event through addon for applications
    BMessage msg(SOFTKM_INJECT_MOUSE_MOVE);
    msg.AddPoint("where", positionToSend);
    msg.AddInt32("buttons", fCurrentButtons);
    msg.AddInt32("modifiers", modifiers);
    SendToMouseAddon(&msg);

    // Edge detection for switching back to macOS
    const float kEdgeThreshold = 5.0f;
    BScreen screen;
    BRect frame = screen.Frame();
    float screenWidth = frame.Width() + 1;
    float screenHeight = frame.Height() + 1;

    // Check if at the configured return edge
    bool atEdge = false;
    switch (fReturnEdge) {
        case EDGE_LEFT:
            atEdge = (fMousePosition.x <= kEdgeThreshold);
            break;
        case EDGE_RIGHT:
            atEdge = (fMousePosition.x >= screenWidth - kEdgeThreshold);
            break;
        case EDGE_TOP:
            atEdge = (fMousePosition.y <= kEdgeThreshold);
            break;
        case EDGE_BOTTOM:
            atEdge = (fMousePosition.y >= screenHeight - kEdgeThreshold);
            break;
    }

    if (atEdge) {
        if (!fAtReturnEdge) {
            // Just entered return edge
            fAtReturnEdge = true;
            fEdgeDwellStart = system_time();
            LOG("Entered return edge %d - starting dwell timer (%.1fs)",
                fReturnEdge, fDwellTime / 1000000.0f);
        } else {
            // Still at return edge - check dwell time
            bigtime_t dwellTime = system_time() - fEdgeDwellStart;
            if (dwellTime >= fDwellTime && fNetworkServer != nullptr) {
                LOG("Return edge dwell complete - switching to macOS");
                // Calculate yRatio (0.0 = top, 1.0 = bottom)
                float yRatio = fMousePosition.y / (screenHeight - 1);
                if (yRatio < 0.0f) yRatio = 0.0f;
                if (yRatio > 1.0f) yRatio = 1.0f;
                LOG("HAIKU→MAC: mouseY=%.0f screenHeight=%.0f → yRatio=%.2f",
                    fMousePosition.y, screenHeight, yRatio);
                // Send clipboard to Mac before switching
                fNetworkServer->SendClipboardSync();
                fNetworkServer->SendControlSwitch(1, yRatio);  // 1 = toMac
                fAtReturnEdge = false;
                fActive = false;
            }
        }
    } else {
        // Not at return edge - reset
        if (fAtReturnEdge) {
            LOG("Return edge - dwell cancelled");
        }
        fAtReturnEdge = false;
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
    LOG("InjectTeamMonitor: showing Team Monitor window");

    // The global gTeamMonitorWindow is declared in TeamMonitorWindow.cpp
    extern TeamMonitorWindow* gTeamMonitorWindow;

    if (gTeamMonitorWindow == NULL) {
        // Create the window - constructor sets gTeamMonitorWindow
        new TeamMonitorWindow();
        LOG("Created new TeamMonitorWindow");
    }

    if (gTeamMonitorWindow != NULL) {
        gTeamMonitorWindow->Enable();
        LOG("TeamMonitorWindow enabled");
    } else {
        LOG("ERROR: Failed to create TeamMonitorWindow");
    }
}

void InputInjector::ProcessEvent(BMessage* message)
{
    // Process events from BMessage (for future use)
    // Currently, events are processed directly from the network server
}

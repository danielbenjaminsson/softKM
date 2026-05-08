#include "SwitchController.h"
#include "../network/NetworkClient.h"
#include "../network/Protocol.h"
#include "../clipboard/ClipboardManager.h"
#include "../Logger.h"

#include <Application.h>
#include <Messenger.h>
#include <Screen.h>
#include <InterfaceDefs.h>
#include <View.h>
#include <game/WindowScreen.h>
#include <OS.h>

#include <cstring>
#include <cstdio>
#include <cmath>

// filter add-on port names (must match SoftKMClientFilter.cpp)
static const char* kCmdPortName   = "softkm_filter_cmd";
static const char* kEventPortName = "softkm_filter_event";

// filter command codes
enum {
    SOFTKM_FILTER_ACTIVATE   = 'fACT',
    SOFTKM_FILTER_DEACTIVATE = 'fDEA',
};

// event what-codes from filter (same as existing add-ons)
enum {
    SOFTKM_EVT_KEY_DOWN    = 'sKdn',
    SOFTKM_EVT_KEY_UP      = 'sKup',
    SOFTKM_EVT_MOUSE_DOWN  = 'sMdn',
    SOFTKM_EVT_MOUSE_UP    = 'sMup',
    SOFTKM_EVT_MOUSE_MOVE  = 'sMmv',
    SOFTKM_EVT_MOUSE_WHEEL = 'sMwh',
};

// edge threshold in pixels (same as InputInjector)
static const float kEdgeThreshold = 5.0f;
// offset from edge when entering capture mode
static const float kEdgeOffset    = 50.0f;

SwitchController::SwitchController()
    : fMode(kMonitoring),
      fSwitchEdge(EDGE_RIGHT),
      fReturnEdge(EDGE_LEFT),
      fDwellTime(300000),
      fAtSwitchEdge(false),
      fEdgeDwellStart(0),
      fLastMousePos(0, 0),
      fLockedCursorPos(0, 0),
      fCurrentButtons(0),
      fCurrentModifiers(0),
      fPendingDX(0), fPendingDY(0),
      fPendingMods(0),
      fHasPending(false),
      fLastSentPos(0, 0),
      fSkipFirstMove(false),
      fClient(nullptr),
      fClipboard(nullptr),
      fEventPort(-1),
      fCmdPort(-1),
      fEventThread(-1),
      fFlushThread(-1),
      fPollThread(-1),
      fRunning(false),
      fScreenW(0),
      fScreenH(0)
{
    fBatchLock = create_sem(1, "softkm_client_batch");

    BScreen screen;
    BRect frame = screen.Frame();
    fScreenW = frame.Width() + 1;
    fScreenH = frame.Height() + 1;
}

SwitchController::~SwitchController()
{
    Stop();
    delete_sem(fBatchLock);
}

status_t SwitchController::Start()
{
    if (fRunning) return B_OK;

    // Create the event port (filter writes here, we read)
    fEventPort = create_port(256, kEventPortName);
    if (fEventPort < 0) {
        LOG("SwitchController: failed to create event port");
        return B_ERROR;
    }

    fRunning = true;

    fEventThread = spawn_thread(EventThreadFunc, "softkm_client_events",
                                B_URGENT_DISPLAY_PRIORITY, this);
    if (fEventThread >= 0) resume_thread(fEventThread);

    fFlushThread = spawn_thread(FlushThreadFunc, "softkm_client_flush",
                                B_NORMAL_PRIORITY, this);
    if (fFlushThread >= 0) resume_thread(fFlushThread);

    fPollThread = spawn_thread(PollThreadFunc, "softkm_client_poll",
                               B_LOW_PRIORITY, this);
    if (fPollThread >= 0) resume_thread(fPollThread);

    LOG("SwitchController: started (event port=%d)", fEventPort);
    return B_OK;
}

void SwitchController::Stop()
{
    if (!fRunning) return;
    fRunning = false;

    DeactivateCapture();

    if (fEventPort >= 0) {
        // Wake up the event thread
        write_port_etc(fEventPort, 0, NULL, 0, B_TIMEOUT, 0);
        delete_port(fEventPort);
        fEventPort = -1;
    }

    if (fEventThread >= 0) {
        status_t ret;
        wait_for_thread(fEventThread, &ret);
        fEventThread = -1;
    }
    if (fFlushThread >= 0) {
        status_t ret;
        wait_for_thread(fFlushThread, &ret);
        fFlushThread = -1;
    }
    if (fPollThread >= 0) {
        status_t ret;
        wait_for_thread(fPollThread, &ret);
        fPollThread = -1;
    }
}

// ------------------------------------------------------------------
// Event-port listener thread
// ------------------------------------------------------------------
int32 SwitchController::EventThreadFunc(void* data)
{
    ((SwitchController*)data)->EventLoop();
    return 0;
}

void SwitchController::EventLoop()
{
    char buf[4096];
    while (fRunning) {
        int32 code;
        ssize_t sz = read_port(fEventPort, &code, buf, sizeof(buf));
        if (sz < 0) {
            if (sz == B_INTERRUPTED) continue;
            break;
        }
        if (sz == 0 && code == 0) continue;

        HandleFilterEvent((uint32)code, buf, sz);
    }
}

void SwitchController::HandleFilterEvent(uint32 what, const char* buf, ssize_t size)
{
    BMessage msg;
    if (size > 0 && msg.Unflatten(buf) != B_OK) return;

    switch (what) {
        case SOFTKM_EVT_MOUSE_MOVE:
        {
            BPoint where;
            int32 buttons = 0, mods = 0;
            msg.FindPoint("where", &where);
            msg.FindInt32("buttons", &buttons);
            msg.FindInt32("modifiers", &mods);
            ProcessMouseMove(where, buttons, mods);
            break;
        }
        case SOFTKM_EVT_MOUSE_DOWN:
        {
            BPoint where;
            int32 buttons = 0, mods = 0, clicks = 1;
            msg.FindPoint("where", &where);
            msg.FindInt32("buttons", &buttons);
            msg.FindInt32("modifiers", &mods);
            msg.FindInt32("clicks", &clicks);
            ProcessMouseDown(where, buttons, mods, clicks);
            break;
        }
        case SOFTKM_EVT_MOUSE_UP:
        {
            BPoint where;
            int32 buttons = 0, mods = 0;
            msg.FindPoint("where", &where);
            msg.FindInt32("buttons", &buttons);
            msg.FindInt32("modifiers", &mods);
            ProcessMouseUp(where, buttons, mods);
            break;
        }
        case SOFTKM_EVT_MOUSE_WHEEL:
        {
            float dx = 0, dy = 0;
            int32 mods = 0;
            msg.FindFloat("delta_x", &dx);
            msg.FindFloat("delta_y", &dy);
            msg.FindInt32("modifiers", &mods);
            ProcessMouseWheel(dx, dy, mods);
            break;
        }
        case SOFTKM_EVT_KEY_DOWN:
        {
            int32 key = 0, mods = 0, rawChar = 0;
            const char* bytes = "";
            msg.FindInt32("key", &key);
            msg.FindInt32("modifiers", &mods);
            msg.FindInt32("raw_char", &rawChar);
            msg.FindString("bytes", &bytes);
            ProcessKeyDown(key, mods, rawChar, bytes ? bytes : "");
            break;
        }
        case SOFTKM_EVT_KEY_UP:
        {
            int32 key = 0, mods = 0;
            msg.FindInt32("key", &key);
            msg.FindInt32("modifiers", &mods);
            ProcessKeyUp(key, mods);
            break;
        }
        default:
            break;
    }
}

// ------------------------------------------------------------------
// Per-event processing
// ------------------------------------------------------------------
void SwitchController::ProcessMouseMove(BPoint where, int32 buttons,
                                        int32 modifiers)
{
    fCurrentButtons  = buttons;
    fCurrentModifiers = modifiers;

    // In monitoring mode — only watch for edge dwell
    if (fMode == kMonitoring) {
        fLastMousePos = where;
        if (IsAtEdge(where, fSwitchEdge)) {
            if (!fAtSwitchEdge) {
                fAtSwitchEdge    = true;
                fEdgeDwellStart  = system_time();
                LOG("Entered switch edge — dwell started");
            } else if ((system_time() - fEdgeDwellStart) >= fDwellTime) {
                LOG("Dwell complete — switching to right Haiku");
                ActivateCapture();
            }
        } else {
            fAtSwitchEdge   = false;
            fEdgeDwellStart = 0;
        }
        return;
    }

    // In capturing mode — compute delta from last known position,
    // batch it for the flush thread.
    float dx = where.x - fLastSentPos.x;
    float dy = where.y - fLastSentPos.y;
    fLastSentPos = where;

    // The first event after activation may carry a stale pre-warp
    // position from the input_server (the filter warps the cursor to
    // the edge, but a B_MOUSE_MOVED that was already in flight will
    // still report the old position). Drop it so we don't emit a huge
    // bogus delta.
    if (fSkipFirstMove) {
        fSkipFirstMove = false;
        LOG("ProcessMouseMove: skipping first stale event (where=%.0f,%.0f)",
            where.x, where.y);
        return;
    }

    acquire_sem(fBatchLock);
    fPendingDX   += dx;
    fPendingDY   += dy;
    fPendingMods  = (uint32)modifiers;
    fHasPending   = true;
    release_sem(fBatchLock);

    // Edge detection for returning to this Haiku
    // (mirrors InputInjector edge detection on the right Haiku)
    if (IsAtEdge(where, fReturnEdge)) {
        if (!fAtSwitchEdge) {
            fAtSwitchEdge   = true;
            fEdgeDwellStart = system_time();
            LOG("At return edge — dwell started");
        } else if ((system_time() - fEdgeDwellStart) >= fDwellTime) {
            float yRatio = where.y / (fScreenH - 1);
            if (yRatio < 0.0f) yRatio = 0.0f;
            if (yRatio > 1.0f) yRatio = 1.0f;
            LOG("Dwell complete — returning to left Haiku yRatio=%.2f", yRatio);

            // Send clipboard first
            if (fClipboard && fClient) {
                uint32 len = 0;
                uint8* data = fClipboard->GetClipboardForSync(&len);
                if (data && len > 0) {
                    fClient->SendClipboard(data, len);
                    delete[] data;
                }
            }
            if (fClient)
                fClient->SendControlSwitch(1, yRatio);  // 1 = return to sender

            DeactivateCapture(yRatio);
        }
    } else {
        fAtSwitchEdge   = false;
        fEdgeDwellStart = 0;
    }
}

void SwitchController::ProcessMouseDown(BPoint where, int32 buttons,
                                        int32 modifiers, int32 clicks)
{
    if (fMode != kCapturing || !fClient) return;
    fCurrentButtons  |= buttons;
    fCurrentModifiers = modifiers;
    fClient->SendMouseDown((uint32)buttons, where.x, where.y,
                           (uint32)modifiers, (uint32)clicks);
}

void SwitchController::ProcessMouseUp(BPoint where, int32 buttons,
                                      int32 modifiers)
{
    if (fMode != kCapturing || !fClient) return;
    fCurrentButtons  &= ~buttons;
    fCurrentModifiers = modifiers;
    fClient->SendMouseUp((uint32)buttons, where.x, where.y, (uint32)modifiers);
}

void SwitchController::ProcessMouseWheel(float dx, float dy, int32 modifiers)
{
    if (fMode != kCapturing || !fClient) return;
    // Invert so direction matches the right Haiku's expectation
    fClient->SendMouseWheel(-dx, -dy, (uint32)modifiers);
}

void SwitchController::ProcessKeyDown(int32 key, int32 modifiers,
                                      int32 rawChar, const char* bytes)
{
    if (fMode != kCapturing || !fClient) return;

    // Build UTF-8 bytes from rawChar if bytes is empty
    char charBuf[8] = {0};
    uint8 numBytes = 0;

    if (bytes && bytes[0] != '\0') {
        size_t len = strlen(bytes);
        if (len > sizeof(charBuf) - 1) len = sizeof(charBuf) - 1;
        memcpy(charBuf, bytes, len);
        numBytes = (uint8)len;
    } else if (rawChar > 0 && rawChar < 128) {
        charBuf[0] = (char)rawChar;
        numBytes = 1;
    }

    // Haiku keycodes are already in Haiku format — send directly
    fClient->SendKeyDown((uint32)key, (uint32)modifiers, charBuf, numBytes);
}

void SwitchController::ProcessKeyUp(int32 key, int32 modifiers)
{
    if (fMode != kCapturing || !fClient) return;
    fClient->SendKeyUp((uint32)key, (uint32)modifiers);
}

// ------------------------------------------------------------------
// Capture activation / deactivation
// ------------------------------------------------------------------
void SwitchController::ActivateCapture()
{
    if (fMode == kCapturing) return;
    if (!fClient || !fClient->IsConnected()) {
        LOG("ActivateCapture: not connected, ignoring");
        return;
    }

    LOG("ActivateCapture: entering capture mode");
    fMode = kCapturing;
    fAtSwitchEdge   = false;
    fEdgeDwellStart = 0;
    fCurrentButtons = 0;

    // Compute yRatio at the current cursor position
    float yRatio = fLastMousePos.y / (fScreenH - 1);
    if (yRatio < 0.0f) yRatio = 0.0f;
    if (yRatio > 1.0f) yRatio = 1.0f;

    // Lock cursor at the switch edge
    BPoint lockPos = EdgeLockPosition(fSwitchEdge);
    fLockedCursorPos = lockPos;
    // IMPORTANT: initialise fLastSentPos to the *current* cursor position,
    // NOT to the edge lock position. Otherwise the first delta computed in
    // ProcessMouseMove would be (currentPos - edgePos), which is enormous
    // (hundreds or thousands of pixels) and would slam the right-Haiku
    // cursor into its return edge, immediately triggering the dwell timer
    // and switching control back. Also skip the very first move event
    // since it may carry a stale pre-warp position from the input_server.
    fLastSentPos = fLastMousePos;
    fSkipFirstMove = true;

    LockCursor(lockPos);
    SendActivateToFilter(lockPos);

    // Send clipboard before switching
    if (fClipboard && fClient) {
        uint32 len = 0;
        uint8* data = fClipboard->GetClipboardForSync(&len);
        if (data && len > 0) {
            fClient->SendClipboard(data, len);
            delete[] data;
        }
    }

    // Notify right Haiku
    fClient->SendControlSwitch(0, yRatio);  // 0 = toRight

    // Notify app UI
    BMessage msg('cACT');
    BMessenger(be_app).SendMessage(&msg);
}

void SwitchController::DeactivateCapture(float yRatio)
{
    if (fMode == kMonitoring) return;

    LOG("DeactivateCapture: returning to monitoring mode yRatio=%.2f", yRatio);
    fMode = kMonitoring;
    fAtSwitchEdge   = false;
    fEdgeDwellStart = 0;
    fCurrentButtons = 0;

    SendDeactivateToFilter();
    UnlockCursor(yRatio);

    // Notify app UI
    BMessage msg('cDEA');
    BMessenger(be_app).SendMessage(&msg);
}

void SwitchController::OnReturnFromRight(float yRatio)
{
    LOG("OnReturnFromRight: yRatio=%.2f", yRatio);
    DeactivateCapture(yRatio);
}

// ------------------------------------------------------------------
// Cursor locking helpers
// ------------------------------------------------------------------
void SwitchController::LockCursor(BPoint edgePos)
{
    set_mouse_position((int32)edgePos.x, (int32)edgePos.y);
    // The filter continuously warps the cursor back to fLockedPos on every
    // B_MOUSE_MOVED event, which effectively locks it to the edge.
}

void SwitchController::UnlockCursor(float yRatio)
{
    // Place cursor near return edge but away from the edge itself
    float x, y;
    const float kOffset = 100.0f;
    y = yRatio * (fScreenH - 1);

    switch (fSwitchEdge) {
        case EDGE_RIGHT:
            x = fScreenW - kOffset;
            break;
        case EDGE_LEFT:
            x = kOffset;
            break;
        case EDGE_TOP:
            x = fScreenW / 2;
            y = kOffset;
            break;
        case EDGE_BOTTOM:
            x = fScreenW / 2;
            y = fScreenH - kOffset;
            break;
        default:
            x = fScreenW - kOffset;
            break;
    }

    if (x < 0) x = 0;
    if (x > fScreenW - 1) x = fScreenW - 1;
    if (y < 0) y = 0;
    if (y > fScreenH - 1) y = fScreenH - 1;

    set_mouse_position((int32)x, (int32)y);
    LOG("UnlockCursor: placed at (%.0f, %.0f)", x, y);
}

// ------------------------------------------------------------------
// Filter communication
// ------------------------------------------------------------------
void SwitchController::SendActivateToFilter(BPoint lockedPos)
{
    if (fCmdPort < 0) {
        fCmdPort = find_port(kCmdPortName);
    }
    if (fCmdPort < 0) {
        LOG("SendActivateToFilter: cmd port not found");
        return;
    }
    // Send as two floats (x, y) to avoid BPoint memcpy issues in the filter
    float coords[2] = { lockedPos.x, lockedPos.y };
    write_port_etc(fCmdPort, SOFTKM_FILTER_ACTIVATE,
                   coords, sizeof(coords),
                   B_TIMEOUT, 100000);
}

void SwitchController::SendDeactivateToFilter()
{
    if (fCmdPort < 0) {
        fCmdPort = find_port(kCmdPortName);
    }
    if (fCmdPort < 0) return;
    write_port_etc(fCmdPort, SOFTKM_FILTER_DEACTIVATE, NULL, 0,
                   B_TIMEOUT, 100000);
}

// ------------------------------------------------------------------
// Edge detection
// ------------------------------------------------------------------
bool SwitchController::IsAtEdge(BPoint pos, uint8 edge) const
{
    switch (edge) {
        case EDGE_RIGHT:  return pos.x >= fScreenW - kEdgeThreshold;
        case EDGE_LEFT:   return pos.x <= kEdgeThreshold;
        case EDGE_TOP:    return pos.y <= kEdgeThreshold;
        case EDGE_BOTTOM: return pos.y >= fScreenH - kEdgeThreshold;
    }
    return false;
}

BPoint SwitchController::EdgeLockPosition(uint8 edge) const
{
    switch (edge) {
        case EDGE_RIGHT:  return BPoint(fScreenW - 1, fLastMousePos.y);
        case EDGE_LEFT:   return BPoint(0, fLastMousePos.y);
        case EDGE_TOP:    return BPoint(fLastMousePos.x, 0);
        case EDGE_BOTTOM: return BPoint(fLastMousePos.x, fScreenH - 1);
    }
    return BPoint(fScreenW - 1, fLastMousePos.y);
}

// ------------------------------------------------------------------
// Poll thread — reads cursor position directly in monitoring mode.
// This is the primary edge-detection path; it does NOT depend on the
// filter forwarding mouse events, so it works even before a full
// input_server restart loads the new filter binary.
// ------------------------------------------------------------------
int32 SwitchController::PollThreadFunc(void* data)
{
    ((SwitchController*)data)->PollLoop();
    return 0;
}

void SwitchController::PollLoop()
{
    // Wait until be_app is fully running before calling get_mouse
    while (fRunning && (be_app == nullptr || be_app->IsLaunching())) {
        snooze(100000);
    }
    snooze(500000);  // Extra 500ms safety margin

    while (fRunning) {
        snooze(16667);  // ~60 Hz

        if (fMode != kMonitoring) continue;  // in capture mode the filter handles it

        BPoint where;
        uint32 buttons = 0;
        if (get_mouse(&where, &buttons) == B_OK) {
            ProcessMouseMove(where, (int32)buttons, 0);
        }
    }
}

// ------------------------------------------------------------------
// Flush thread — sends batched mouse deltas at ~60 Hz
// ------------------------------------------------------------------
int32 SwitchController::FlushThreadFunc(void* data)
{
    ((SwitchController*)data)->FlushLoop();
    return 0;
}

void SwitchController::FlushLoop()
{
    // Throttled diagnostics: log a summary every ~1s so we can see
    // whether the flush thread is actually flushing real deltas to
    // the right Haiku, without flooding the log with one entry per
    // mouse event.
    bigtime_t  lastSummary  = system_time();
    int32      flushCount   = 0;
    int32      emptyCount   = 0;
    float      sumAbsDx     = 0;
    float      sumAbsDy     = 0;

    while (fRunning) {
        snooze(16667);  // ~60 Hz

        if (fMode != kCapturing || !fClient) {
            // Reset counters when we leave capture mode so the next
            // session starts with a clean slate.
            flushCount = emptyCount = 0;
            sumAbsDx = sumAbsDy = 0;
            lastSummary = system_time();
            continue;
        }

        acquire_sem(fBatchLock);
        bool   hasPending = fHasPending;
        float  dx         = fPendingDX;
        float  dy         = fPendingDY;
        uint32 mods       = fPendingMods;
        fPendingDX  = 0;
        fPendingDY  = 0;
        fHasPending = false;
        release_sem(fBatchLock);

        if (!hasPending) {
            emptyCount++;
        } else if (dx != 0 || dy != 0) {
            fClient->SendMouseMove(dx, dy, true, mods);
            flushCount++;
            sumAbsDx += (dx < 0 ? -dx : dx);
            sumAbsDy += (dy < 0 ? -dy : dy);
        }

        // 1Hz summary
        bigtime_t now = system_time();
        if (now - lastSummary >= 1000000) {
            LOG("FlushLoop: 1s summary — sent=%ld empty=%ld absDx=%.1f absDy=%.1f connected=%d",
                flushCount, emptyCount, sumAbsDx, sumAbsDy,
                fClient ? (int)fClient->IsConnected() : -1);
            flushCount = emptyCount = 0;
            sumAbsDx = sumAbsDy = 0;
            lastSummary = now;
        }
    }
}

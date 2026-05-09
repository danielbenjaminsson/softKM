/*
 * SoftKMClientFilter — BInputServerFilter add-on
 *
 * When the left-Haiku app ("SoftKMClient") is in CAPTURING mode it
 * signals this filter via a named port.  The filter then intercepts
 * every keyboard / mouse event and forwards it to the app instead of
 * letting it reach the desktop.
 *
 * Communication with the main app:
 *   Port "softkm_filter_cmd"   — app → filter  (commands)
 *   Port "softkm_filter_event" — filter → app  (raw events as BMessages)
 *
 * Commands written to softkm_filter_cmd:
 *   code SOFTKM_FILTER_ACTIVATE   — start capturing
 *   code SOFTKM_FILTER_DEACTIVATE — stop capturing
 */

#include <InputServerFilter.h>
#include <List.h>
#include <Message.h>
#include <OS.h>
#include <Point.h>
#include <Screen.h>
#include <InterfaceDefs.h>
#include <game/WindowScreen.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <math.h>

static void DebugLog(const char* fmt, ...)
{
    FILE* f = fopen("/boot/home/softkm_filter.log", "a");
    if (!f) return;
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    fprintf(f, "[%02d:%02d:%02d] ", t->tm_hour, t->tm_min, t->tm_sec);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

enum {
    SOFTKM_FILTER_ACTIVATE   = 'fACT',
    SOFTKM_FILTER_DEACTIVATE = 'fDEA',
};

// Event type codes sent to the app (same What codes as the existing add-ons)
enum {
    SOFTKM_EVT_KEY_DOWN    = 'sKdn',
    SOFTKM_EVT_KEY_UP      = 'sKup',
    SOFTKM_EVT_MOUSE_DOWN  = 'sMdn',
    SOFTKM_EVT_MOUSE_UP    = 'sMup',
    SOFTKM_EVT_MOUSE_MOVE  = 'sMmv',
    SOFTKM_EVT_MOUSE_WHEEL = 'sMwh',
};

static const char* kCmdPortName   = "softkm_filter_cmd";
static const char* kEventPortName = "softkm_filter_event";

class SoftKMClientFilter : public BInputServerFilter {
public:
    SoftKMClientFilter();
    virtual ~SoftKMClientFilter();

    virtual status_t InitCheck();

    virtual filter_result Filter(BMessage* message, BList* outList);

private:
    // Background command-listener thread
    static int32 CmdThreadFunc(void* data);
    void         CmdLoop();

    bool     fActive;       // currently capturing
    port_id  fCmdPort;      // receives ACTIVATE / DEACTIVATE
    port_id  fEventPort;    // sends events to main app (-1 if not found)
    thread_id fCmdThread;
    bool     fRunning;

    // Cursor lock position (where the visual cursor is pinned to
    // during capture so the user doesn't see it wander on the
    // sender). Used by the event-replacement code; NOT used to
    // compute mouse deltas.
    BPoint   fLockedPos;

    // Most recently observed mouse-button set (currently-held bitmap).
    // Haiku's B_MOUSE_DOWN/UP report this *post-event* set in the
    // 'buttons' field, but the wire protocol expects "the button that
    // changed" (1 bit). We compute the changed bit from
    // (fLastButtons XOR currentButtons) and update fLastButtons
    // accordingly. Without this, B_MOUSE_UP arrives with buttons=0
    // (nothing held), the receiver does fCurrentButtons &= ~0 which
    // is a no-op, the receiver thinks the button is still held, and
    // context menus stop dismissing on outside click.
    int32    fLastButtons;

    // One-shot diagnostic: log the first B_MOUSE_MOVED event's field
    // names after each ACTIVATE so we can confirm which fields the
    // input_server attaches in this Haiku build (so a future hrev
    // change leaves a paper trail in the filter log).
    bool     fLoggedMouseFields;

    // Last seen key-state bitmap from B_MODIFIERS_CHANGED. We diff
    // each new states blob against this to determine the exact raw
    // keycode that toggled — more reliable than guessing from the
    // modifier mask, which can be ambiguous on weird keymaps.
    uint8    fLastKeyStates[16];

    port_id  FindEventPort();
    void     SendEvent(BMessage* evt);
};

// ---------------------------------------------------------------------------
SoftKMClientFilter::SoftKMClientFilter()
    : BInputServerFilter(),
      fActive(false),
      fCmdPort(-1),
      fEventPort(-1),
      fCmdThread(-1),
      fRunning(false),
      fLockedPos(0, 0),
      fLastButtons(0),
      fLoggedMouseFields(false)
{
    memset(fLastKeyStates, 0, sizeof(fLastKeyStates));
}

SoftKMClientFilter::~SoftKMClientFilter()
{
    fRunning = false;
    if (fCmdPort >= 0) {
        // Wake up the thread
        write_port_etc(fCmdPort, 0, NULL, 0, B_TIMEOUT, 0);
        delete_port(fCmdPort);
        fCmdPort = -1;
    }
    if (fCmdThread >= 0) {
        status_t ret;
        wait_for_thread(fCmdThread, &ret);
    }
}

status_t SoftKMClientFilter::InitCheck()
{
    DebugLog("=== SoftKMClientFilter initializing ===");

    fCmdPort = create_port(32, kCmdPortName);
    if (fCmdPort < 0) {
        DebugLog("Failed to create cmd port");
        return B_ERROR;
    }

    fRunning = true;
    fCmdThread = spawn_thread(CmdThreadFunc, "softkm_filter_cmd",
                              B_NORMAL_PRIORITY, this);
    if (fCmdThread >= 0)
        resume_thread(fCmdThread);

    DebugLog("SoftKMClientFilter ready, cmd port=%d", fCmdPort);
    return B_OK;
}

port_id SoftKMClientFilter::FindEventPort()
{
    return find_port(kEventPortName);
}

// ---------------------------------------------------------------------------
// Command thread — watches for ACTIVATE / DEACTIVATE from main app
// ---------------------------------------------------------------------------
int32 SoftKMClientFilter::CmdThreadFunc(void* data)
{
    ((SoftKMClientFilter*)data)->CmdLoop();
    return 0;
}

void SoftKMClientFilter::CmdLoop()
{
    while (fRunning) {
        int32 code;
        char buf[256];
        ssize_t sz = read_port(fCmdPort, &code, buf, sizeof(buf));
        if (sz < 0) {
            if (sz == B_INTERRUPTED) continue;
            break;
        }

        if (code == SOFTKM_FILTER_ACTIVATE) {
            // Payload: BPoint with locked cursor position
            BPoint locked(0, 0);
            if (sz >= (ssize_t)sizeof(BPoint)) {
                float coords[2];
                memcpy(coords, buf, sizeof(coords));
                locked.x = coords[0];
                locked.y = coords[1];
            }
            fLockedPos   = locked;
            fLastButtons = 0;     // assume no buttons held at activation
            fLoggedMouseFields = false;  // re-arm one-shot diagnostic
            memset(fLastKeyStates, 0, sizeof(fLastKeyStates));
            fEventPort = FindEventPort();
            fActive = true;
            DebugLog("ACTIVATED — event port=%d locked=(%.0f,%.0f)",
                fEventPort, locked.x, locked.y);
        } else if (code == SOFTKM_FILTER_DEACTIVATE) {
            fActive = false;
            DebugLog("DEACTIVATED");
        }
    }
}

// ---------------------------------------------------------------------------
// Helper: send a flattened BMessage to the event port. Re-resolves the
// port lazily and on every write failure, so a softKMClient restart
// (which creates a fresh port with the same name) is picked up
// transparently without needing an input_server reload.
// ---------------------------------------------------------------------------
void SoftKMClientFilter::SendEvent(BMessage* evt)
{
    if (fEventPort < 0) {
        fEventPort = FindEventPort();
        if (fEventPort >= 0)
            DebugLog("Found event port: %d", fEventPort);
    }
    if (fEventPort < 0) return;

    ssize_t flat = evt->FlattenedSize();
    char* buf = new char[flat];
    bool ok = (evt->Flatten(buf, flat) == B_OK);

    if (ok) {
        status_t r = write_port_etc(fEventPort, evt->what, buf, flat,
                                    B_TIMEOUT, 50000);
        if (r != B_OK) {
            // Port invalid (peer died) or full. Drop the event but
            // invalidate the cached port id so the next event will
            // re-resolve. The new softKMClient will have created a
            // fresh port with the same name.
            DebugLog("write_port failed (%s) on what=0x%x — dropping cached port",
                strerror(r), (unsigned)evt->what);
            fEventPort = -1;
        }
    }
    delete[] buf;
}

// ---------------------------------------------------------------------------
// Filter — called by input_server for every event
// ---------------------------------------------------------------------------
filter_result SoftKMClientFilter::Filter(BMessage* message, BList* outList)
{
    // Lazy/refresh resolution of the event port happens inside SendEvent.

    if (!fActive) {
        // Even in monitoring mode, forward mouse position to the app
        // so it can detect edge dwell and trigger capture.
        if (message->what == B_MOUSE_MOVED) {
            BPoint where;
            int32 buttons = 0, modifiers = 0;
            message->FindPoint("where", &where);
            message->FindInt32("buttons", &buttons);
            message->FindInt32("modifiers", &modifiers);

            BMessage evt(SOFTKM_EVT_MOUSE_MOVE);
            evt.AddPoint("where", where);
            evt.AddInt32("buttons", buttons);
            evt.AddInt32("modifiers", modifiers);
            SendEvent(&evt);
        }
        return B_DISPATCH_MESSAGE;  // Always pass through in monitoring mode
    }

    bool consumed = false;

    switch (message->what) {
        // ---- Keyboard ----
        case B_KEY_DOWN:
        case B_UNMAPPED_KEY_DOWN:
        {
            int32 key = 0, modifiers = 0, rawChar = 0;
            message->FindInt32("key", &key);
            message->FindInt32("modifiers", &modifiers);
            message->FindInt32("raw_char", &rawChar);
            const char* bytes = "";
            message->FindString("bytes", &bytes);

            BMessage evt(SOFTKM_EVT_KEY_DOWN);
            evt.AddInt32("key", key);
            evt.AddInt32("modifiers", modifiers);
            evt.AddInt32("raw_char", rawChar);
            evt.AddString("bytes", bytes ? bytes : "");
            SendEvent(&evt);
            consumed = true;
            break;
        }

        case B_KEY_UP:
        case B_UNMAPPED_KEY_UP:
        {
            int32 key = 0, modifiers = 0;
            message->FindInt32("key", &key);
            message->FindInt32("modifiers", &modifiers);

            BMessage evt(SOFTKM_EVT_KEY_UP);
            evt.AddInt32("key", key);
            evt.AddInt32("modifiers", modifiers);
            SendEvent(&evt);
            consumed = true;
            break;
        }

        case B_MODIFIERS_CHANGED:
        {
            // Haiku reports modifier-key transitions (Shift, Ctrl, Alt,
            // etc.) only via B_MODIFIERS_CHANGED — *not* as B_KEY_DOWN /
            // B_KEY_UP — so without forwarding these the receiver never
            // learns that a modifier is held. That breaks any
            // modifier-only press (e.g. Ctrl alone) and also means that
            // combos like Ctrl+Alt+T arrive on the receiver as a bare
            // 'T' because the receiver's input_server never saw the
            // matching modifier-down events.
            //
            // We previously inferred which physical modifier key
            // changed by XORing `modifiers` with `be:old_modifiers`
            // and looking up the toggled bit in a hand-coded mask ->
            // keycode table. That broke spectacularly on this taurus
            // keymap because pressing physical Ctrl ALSO toggled the
            // B_LEFT_SHIFT_KEY bit (so the filter sent both Ctrl and
            // Shift down events), and the masks for various keymaps
            // disagree on which bit corresponds to which physical key.
            //
            // The robust fix is to use the 16-byte `states` blob
            // (Haiku's key_map-style 128-bit keyboard state bitmap)
            // that B_MODIFIERS_CHANGED carries: diff it against the
            // last known state to find exactly which keycode bit
            // toggled — that IS the raw keycode of the physical key
            // that the user pressed or released, with no keymap
            // guesswork required.
            int32 newMods = 0;
            message->FindInt32("modifiers", &newMods);

            ssize_t statesLen = 0;
            const void* statesData = nullptr;
            if (message->FindData("states", B_RAW_TYPE,
                    &statesData, &statesLen) != B_OK
                || statesLen < (ssize_t)sizeof(fLastKeyStates)) {
                // No states blob (older hrev?) — fall back to no-op.
                // The receiver will at least see the modifier mask
                // come along with subsequent B_KEY_DOWN events.
                consumed = true;
                break;
            }

            const uint8* newStates = (const uint8*)statesData;
            for (size_t byte = 0; byte < sizeof(fLastKeyStates); byte++) {
                uint8 diff = newStates[byte] ^ fLastKeyStates[byte];
                if (diff == 0) continue;
                for (int bit = 0; bit < 8; bit++) {
                    if ((diff & (1 << bit)) == 0) continue;
                    // Haiku's key_map states use big-endian bit order
                    // within each byte: bit 7 of byte 0 is keycode 0,
                    // bit 6 of byte 0 is keycode 1, etc.
                    int32 keycode = (int32)byte * 8 + (7 - bit);
                    bool pressed = (newStates[byte] & (1 << bit)) != 0;

                    BMessage evt(pressed ? SOFTKM_EVT_KEY_DOWN
                                         : SOFTKM_EVT_KEY_UP);
                    evt.AddInt32("key", keycode);
                    evt.AddInt32("modifiers", newMods);
                    if (pressed) {
                        evt.AddInt32("raw_char", 0);
                        evt.AddString("bytes", "");
                    }
                    SendEvent(&evt);
                }
            }
            memcpy(fLastKeyStates, newStates, sizeof(fLastKeyStates));
            consumed = true;
            break;
        }

        // ---- Mouse move ----
        case B_MOUSE_MOVED:
        {
            BPoint where;
            int32 buttons = 0, modifiers = 0;
            int32 dx = 0, dy = 0;
            message->FindPoint("where", &where);
            message->FindInt32("buttons", &buttons);
            message->FindInt32("modifiers", &modifiers);

            // Read the *raw* mouse deltas that MouseInputDevice
            // attached to this message. These come from the kernel
            // driver's mouse_movement struct (xdelta/ydelta from the
            // ioctl MS_READ) — i.e. the actual motion the device
            // reported, BEFORE the input_server turned it into a
            // cursor position and clamped it to the screen.
            //
            // Using these raw deltas (the macOS equivalent is
            // CGEvent's mouseEventDeltaX/Y field) sidesteps every
            // pain point we hit trying to derive motion from
            // 'where':
            //   - no edge-clamping (where the OS pins where.x to
            //     screenWidth-1 and rightward physical motion
            //     becomes invisible);
            //   - no need to set_mouse_position from inside the
            //     filter (which is racy and sometimes a no-op);
            //   - no warp-echo or rebound detection;
            //   - the cursor can be wherever, doesn't matter.
            //
            // Found by greping Haiku source: src/add-ons/input_server
            // /devices/mouse/MouseInputDevice.cpp builds the
            // B_MOUSE_MOVED message with AddInt32("be:delta_x", ...)
            // / AddInt32("be:delta_y", ...) before EnqueueMessage.
            // Filters run after the device add-on, so we see those
            // fields intact.
            bool haveRawDeltas = (
                message->FindInt32("be:delta_x", &dx) == B_OK
             && message->FindInt32("be:delta_y", &dy) == B_OK);

            // be:delta_y on this Haiku build follows the kernel
            // driver's PS/2-style convention: positive ydelta means
            // the user moved the mouse UP on the desk (cursor
            // toward smaller screen Y). Haiku's screen Y axis is
            // top-down (positive = down), so we have to negate to
            // get a screen-convention delta. Confirmed empirically
            // by per-event logging that compares raw dy to
            // (where.y - lock.y): when the cursor visibly moves
            // up on screen, raw dy is positive while whereDy is
            // negative, so they have opposite signs and we negate
            // to align with the where-derived sign that the
            // receiver expects.
            dy = -dy;

            // One-shot diagnostic: log the field names of the first
            // mouse-move event after each activation, so we can see
            // exactly what the input_server attaches on this hrev.
            // Keeps a record in the filter log if a future Haiku
            // revision renames or removes be:delta_x/y.
            if (!fLoggedMouseFields) {
                fLoggedMouseFields = true;
                char nameBuf[256] = "";
                size_t off = 0;
                char* name;
                type_code type;
                int32 count;
                for (int32 i = 0; message->GetInfo(B_ANY_TYPE, i,
                        &name, &type, &count) == B_OK; i++) {
                    int n = snprintf(nameBuf + off, sizeof(nameBuf) - off,
                        " %s(0x%x)", name, (unsigned)type);
                    if (n < 0 || (size_t)n >= sizeof(nameBuf) - off) break;
                    off += n;
                }
                DebugLog("First B_MOUSE_MOVED fields:%s", nameBuf);
                DebugLog("  haveRawDeltas=%d  raw_dx=%d  raw_dy=%d (after negate)",
                    (int)haveRawDeltas, (int)dx, (int)dy);
            }

            if (haveRawDeltas) {
                if (dx != 0 || dy != 0) {
                    BMessage evt(SOFTKM_EVT_MOUSE_MOVE);
                    evt.AddPoint("where", where);
                    evt.AddFloat("dx", (float)dx);
                    evt.AddFloat("dy", (float)dy);
                    evt.AddInt32("buttons", buttons);
                    evt.AddInt32("modifiers", modifiers);
                    SendEvent(&evt);
                }
            } else {
                // Fallback for hrev that doesn't expose be:delta_x/y.
                // Log once so we know we're on this path; events are
                // dropped in this mode (we used to derive deltas from
                // 'where' here but it was so unreliable that losing
                // mouse motion entirely is preferable to the phantom
                // drift we used to see).
                static bool logged = false;
                if (!logged) {
                    DebugLog("WARNING: B_MOUSE_MOVED has no be:delta_x — "
                        "mouse motion forwarding disabled. Update Haiku "
                        "or fall back to the position-derived delta path.");
                    logged = true;
                }
            }

            // Replace the original event with one pinned at the lock
            // position so downstream apps (Tracker, Deskbar, etc) don't
            // see the cursor moving across the sender's screen during
            // capture. We do NOT call set_mouse_position any more — the
            // physical OS cursor will drift wherever the user pushes it,
            // but it's still pinned visually because the desktop only
            // sees the replacement event with where=fLockedPos.
            //
            // (When we used to call set_mouse_position here, the warp
            // was racy and produced echo events that polluted the
            // delta calculation. Now that deltas come from the driver
            // directly, we have no need to re-warp.)
            BMessage* replacement = new BMessage(B_MOUSE_MOVED);
            replacement->AddInt64("when", system_time());
            replacement->AddPoint("where", fLockedPos);
            replacement->AddInt32("buttons", buttons);
            replacement->AddInt32("modifiers", modifiers);
            outList->AddItem(replacement);
            return B_SKIP_MESSAGE;   // discard original, deliver replacement
        }

        // ---- Mouse buttons ----
        case B_MOUSE_DOWN:
        {
            BPoint where;
            int32 buttons = 0, modifiers = 0, clicks = 1;
            message->FindPoint("where", &where);
            message->FindInt32("buttons", &buttons);
            message->FindInt32("modifiers", &modifiers);
            message->FindInt32("clicks", &clicks);

            // Haiku reports 'buttons' as the post-event held set. The
            // wire protocol's MouseDown/Up payload uses macOS-style
            // semantics where 'buttons' is the single bit that just
            // changed (the receiver does fCurrentButtons |= bit on
            // down and &= ~bit on up). Compute the bit that newly
            // turned on as (buttons & ~fLastButtons) — usually one
            // bit, but if multiple buttons go down in the same event
            // we still send the union of new bits, which the receiver
            // will OR in correctly.
            int32 changed = buttons & ~fLastButtons;
            fLastButtons = buttons;

            BMessage evt(SOFTKM_EVT_MOUSE_DOWN);
            evt.AddPoint("where", where);
            evt.AddInt32("buttons", changed);
            evt.AddInt32("modifiers", modifiers);
            evt.AddInt32("clicks", clicks);
            SendEvent(&evt);
            consumed = true;
            break;
        }

        case B_MOUSE_UP:
        {
            BPoint where;
            int32 buttons = 0, modifiers = 0;
            message->FindPoint("where", &where);
            message->FindInt32("buttons", &buttons);
            message->FindInt32("modifiers", &modifiers);

            // Bits that were held before but aren't now == bits that
            // just released. See B_MOUSE_DOWN above for rationale.
            int32 changed = fLastButtons & ~buttons;
            fLastButtons = buttons;

            BMessage evt(SOFTKM_EVT_MOUSE_UP);
            evt.AddPoint("where", where);
            evt.AddInt32("buttons", changed);
            evt.AddInt32("modifiers", modifiers);
            SendEvent(&evt);
            consumed = true;
            break;
        }

        case B_MOUSE_WHEEL_CHANGED:
        {
            float dx = 0, dy = 0;
            int32 modifiers = 0;
            message->FindFloat("be:wheel_delta_x", &dx);
            message->FindFloat("be:wheel_delta_y", &dy);
            message->FindInt32("modifiers", &modifiers);

            BMessage evt(SOFTKM_EVT_MOUSE_WHEEL);
            evt.AddFloat("delta_x", dx);
            evt.AddFloat("delta_y", dy);
            evt.AddInt32("modifiers", modifiers);
            SendEvent(&evt);
            consumed = true;
            break;
        }

        default:
            break;
    }

    return consumed ? B_SKIP_MESSAGE : B_DISPATCH_MESSAGE;
}

extern "C" BInputServerFilter* instantiate_input_filter()
{
    return new SoftKMClientFilter();
}

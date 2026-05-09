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

    // Mouse cursor locking (hide+lock to edge pixel while capturing)
    BPoint   fLockedPos;
    // Most recently observed OS-reported cursor position. Deltas are
    // computed event-to-event against this, NOT against fLockedPos:
    // the filter cannot reliably warp the cursor back to fLockedPos
    // (set_mouse_position races with input_server's own tracking and
    // its effect is often ignored), so a fixed-reference scheme would
    // produce huge bogus deltas equal to the distance between the OS
    // cursor and the lock point. Tracking 'last seen' makes deltas
    // robust to whatever the cursor is actually doing.
    BPoint   fLastPos;

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
      fLastPos(0, 0),
      fLastButtons(0)
{
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
            fLockedPos = locked;
            fLastPos   = locked;  // seed so the very first event's delta
                                  // is computed against the lock point,
                                  // not against (0,0)
            fLastButtons = 0;     // assume no buttons held at activation
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
            // modifier-only press (e.g. Ctrl alone to a CAD app), and
            // also means that combos like Ctrl+Alt+T arrive on the
            // receiver as a bare 'T' because the receiver's input_server
            // never saw the matching modifier-down events.
            //
            // We translate each toggled bit into a synthetic
            // SOFTKM_EVT_KEY_DOWN or _KEY_UP carrying the corresponding
            // Haiku keycode, so the receiver can inject a real
            // KEY_DOWN/UP for that physical modifier key.
            int32 newMods = 0, oldMods = 0;
            message->FindInt32("modifiers", &newMods);
            message->FindInt32("be:old_modifiers", &oldMods);
            int32 changed = newMods ^ oldMods;

            // Modifier bit -> Haiku raw keycode mapping. Pairs of
            // (modifier_mask, keycode); see InterfaceDefs.h for the
            // mask values. Using the left-specific masks rather than
            // the generic ones (B_SHIFT_KEY, B_CONTROL_KEY, …) gives
            // us per-side resolution so left/right Ctrl etc. inject
            // as the correct physical key on the receiver.
            static const struct { int32 mask; int32 keycode; } kModMap[] = {
                { 0x1000,  0x4b },  // B_LEFT_SHIFT_KEY
                { 0x2000,  0x56 },  // B_RIGHT_SHIFT_KEY
                { 0x10000, 0x5c },  // B_LEFT_CONTROL_KEY
                { 0x20000, 0x60 },  // B_RIGHT_CONTROL_KEY
                { 0x40000, 0x5d },  // B_LEFT_OPTION_KEY  (Alt)
                { 0x80000, 0x5f },  // B_RIGHT_OPTION_KEY (Alt Gr)
                { 0x4000,  0x66 },  // B_LEFT_COMMAND_KEY (Win/Cmd)
                { 0x8000,  0x67 },  // B_RIGHT_COMMAND_KEY
                { 0x08,    0x3b },  // B_CAPS_LOCK
                { 0x20,    0x22 },  // B_NUM_LOCK
                { 0x10,    0x0f },  // B_SCROLL_LOCK
            };

            for (size_t i = 0; i < sizeof(kModMap) / sizeof(kModMap[0]); i++) {
                if ((changed & kModMap[i].mask) == 0)
                    continue;
                bool pressed = (newMods & kModMap[i].mask) != 0;

                BMessage evt(pressed ? SOFTKM_EVT_KEY_DOWN
                                     : SOFTKM_EVT_KEY_UP);
                evt.AddInt32("key", kModMap[i].keycode);
                evt.AddInt32("modifiers", newMods);
                if (pressed) {
                    // Pressing a modifier produces no character, but
                    // SwitchController unconditionally reads raw_char /
                    // bytes for KEY_DOWN — populate them explicitly so
                    // we don't end up with stale stack values.
                    evt.AddInt32("raw_char", 0);
                    evt.AddString("bytes", "");
                }
                SendEvent(&evt);
            }
            consumed = true;
            break;
        }

        // ---- Mouse move ----
        case B_MOUSE_MOVED:
        {
            BPoint where;
            int32 buttons = 0, modifiers = 0;
            message->FindPoint("where", &where);
            message->FindInt32("buttons", &buttons);
            message->FindInt32("modifiers", &modifiers);

            // Two kinds of events arrive in this handler while we
            // are capturing:
            //
            //   1. Real user motion: where = (cursor's actual current
            //      position). Forward to the controller as
            //      dx = where - fLastPos.
            //
            //   2. Warp echoes: input_server delivers a B_MOUSE_MOVED in
            //      response to our own set_mouse_position(lockedPos, …)
            //      call below. Such an event arrives with where ==
            //      fLockedPos exactly. We MUST drop these, otherwise
            //      every real +N delta would be followed by a −N echo
            //      and the receiver would never see net motion.
            //
            // Why event-to-event (where - fLastPos) and not
            // lock-relative (where - fLockedPos)? Because
            // set_mouse_position from inside the filter is racy: it
            // sometimes silently does nothing, leaving the OS cursor
            // camped wherever it was. With a lock-relative formula
            // the filter would then forward a constant dx of ~100px
            // on every event, flooding the receiver with phantom
            // motion. Event-to-event survives a failed warp because
            // dx tracks actual motion between consecutive reported
            // positions regardless of where they are on screen.
            const float kWarpEpsilon = 0.5f;
            bool isWarpEcho = (fabsf(where.x - fLockedPos.x) < kWarpEpsilon
                            && fabsf(where.y - fLockedPos.y) < kWarpEpsilon);

            if (!isWarpEcho) {
                float dx = where.x - fLastPos.x;
                float dy = where.y - fLastPos.y;
                fLastPos = where;

                BMessage evt(SOFTKM_EVT_MOUSE_MOVE);
                evt.AddPoint("where", where);
                evt.AddFloat("dx", dx);
                evt.AddFloat("dy", dy);
                evt.AddInt32("buttons", buttons);
                evt.AddInt32("modifiers", modifiers);
                SendEvent(&evt);
            } else {
                // Warp echo: realign fLastPos to the lock point so the
                // next real-motion delta is computed from there. Don't
                // forward the event.
                fLastPos = fLockedPos;
            }

            // Best-effort warp back to the lock position. When it
            // works, the next event arrives as a warp echo (caught
            // above) and the cursor stays visually pinned. When it
            // doesn't, the event-to-event delta calc still produces
            // correct motion — we just lose the visual pin and the
            // cursor may drift across the screen on the sender (which
            // is invisible to the user since the receiver's cursor
            // is what they're watching).
            set_mouse_position((int32)fLockedPos.x, (int32)fLockedPos.y);

            // Replace the original event with one pinned at the lock
            // position so downstream handlers (like the desktop) don't
            // see the cursor moving.
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

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
            // Forward as key-down/up for each changed modifier
            // (handled in SwitchController via the key events above)
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

            // Compute the user's motion as where - fLockedPos. This
            // works because we warp the cursor back to fLockedPos on
            // every event below: in steady state the OS cursor is
            // sitting at fLockedPos, the user nudges it, and we see
            // a single B_MOUSE_MOVED with where = lockedPos + delta.
            //
            // Two events to be aware of:
            //   1. Pure warp echoes — when input_server delivers our
            //      own warp as a B_MOUSE_MOVED, where == fLockedPos
            //      exactly. dx/dy are zero anyway, but we drop them
            //      explicitly to avoid spamming the controller.
            //   2. Coalesced events — input_server may merge the
            //      warp's position update with subsequent user motion
            //      into one event. That event reports where = lockedPos
            //      + (user motion since warp), which is exactly the
            //      delta we want. So dx = where.x - fLockedPos.x
            //      gives the user's motion regardless of whether the
            //      warp's echo arrived as a separate event or got
            //      coalesced into this one.
            //
            // Computing against fLockedPos (rather than fLastPos =
            // previous reported where) avoids the doubling bug we saw
            // when y-axis motion made the echo's y fail to match
            // fLockedPos.y exactly: the echo would slip through as
            // 'real motion' with dx = fLockedPos.x - lastWhere.x
            // (a fake negative), and the next user event would then
            // be lastWhere - fLockedPos (a fake positive of the same
            // magnitude), doubling X displacement.
            float dx = where.x - fLockedPos.x;
            float dy = where.y - fLockedPos.y;

            const float kEpsilon = 0.5f;
            bool isZero = (fabsf(dx) < kEpsilon && fabsf(dy) < kEpsilon);

            if (!isZero) {
                BMessage evt(SOFTKM_EVT_MOUSE_MOVE);
                evt.AddPoint("where", where);
                evt.AddFloat("dx", dx);
                evt.AddFloat("dy", dy);
                evt.AddInt32("buttons", buttons);
                evt.AddInt32("modifiers", modifiers);
                SendEvent(&evt);
            }

            // Warp cursor back to lock position. This usually works
            // (the OS cursor visibly stays near fLockedPos in the
            // logs), so the next event will report a small delta from
            // the lock again. Even if it occasionally fails, the
            // worst case is one event with a large dx — not a sustained
            // doubled-displacement bug.
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

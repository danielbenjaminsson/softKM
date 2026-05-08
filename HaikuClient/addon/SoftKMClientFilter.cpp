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

    port_id  FindEventPort();
};

// ---------------------------------------------------------------------------
SoftKMClientFilter::SoftKMClientFilter()
    : BInputServerFilter(),
      fActive(false),
      fCmdPort(-1),
      fEventPort(-1),
      fCmdThread(-1),
      fRunning(false),
      fLockedPos(0, 0)
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
// Filter — called by input_server for every event
// ---------------------------------------------------------------------------
filter_result SoftKMClientFilter::Filter(BMessage* message, BList* outList)
{
    // Always try to find the event port if we don't have it yet
    if (fEventPort < 0) {
        fEventPort = FindEventPort();
        if (fEventPort >= 0)
            DebugLog("Found event port: %d", fEventPort);
    }

    if (!fActive) {
        // Even in monitoring mode, forward mouse position to the app
        // so it can detect edge dwell and trigger capture.
        if (message->what == B_MOUSE_MOVED && fEventPort >= 0) {
            BPoint where;
            int32 buttons = 0, modifiers = 0;
            message->FindPoint("where", &where);
            message->FindInt32("buttons", &buttons);
            message->FindInt32("modifiers", &modifiers);

            BMessage evt(SOFTKM_EVT_MOUSE_MOVE);
            evt.AddPoint("where", where);
            evt.AddInt32("buttons", buttons);
            evt.AddInt32("modifiers", modifiers);

            ssize_t flat = evt.FlattenedSize();
            char* buf2 = new char[flat];
            if (evt.Flatten(buf2, flat) == B_OK)
                write_port_etc(fEventPort, evt.what, buf2, flat,
                               B_TIMEOUT, 0);  // non-blocking — drop if full
            delete[] buf2;
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

            if (fEventPort >= 0) {
                BMessage evt(SOFTKM_EVT_KEY_DOWN);
                evt.AddInt32("key", key);
                evt.AddInt32("modifiers", modifiers);
                evt.AddInt32("raw_char", rawChar);
                evt.AddString("bytes", bytes ? bytes : "");

                ssize_t flat = evt.FlattenedSize();
                char* buf2 = new char[flat];
                if (evt.Flatten(buf2, flat) == B_OK)
                    write_port_etc(fEventPort, evt.what, buf2, flat,
                                   B_TIMEOUT, 50000);
                delete[] buf2;
            }
            consumed = true;
            break;
        }

        case B_KEY_UP:
        case B_UNMAPPED_KEY_UP:
        {
            int32 key = 0, modifiers = 0;
            message->FindInt32("key", &key);
            message->FindInt32("modifiers", &modifiers);

            if (fEventPort >= 0) {
                BMessage evt(SOFTKM_EVT_KEY_UP);
                evt.AddInt32("key", key);
                evt.AddInt32("modifiers", modifiers);

                ssize_t flat = evt.FlattenedSize();
                char* buf2 = new char[flat];
                if (evt.Flatten(buf2, flat) == B_OK)
                    write_port_etc(fEventPort, evt.what, buf2, flat,
                                   B_TIMEOUT, 50000);
                delete[] buf2;
            }
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

            // Compute the user's movement *relative to the locked
            // position*. Because we warp the cursor back to fLockedPos
            // on every real motion event, any reported 'where' that
            // differs from fLockedPos represents the user's most recent
            // physical motion. After we warp back, the next event
            // starts again from fLockedPos, so deltas never need to
            // accumulate over multiple events.
            float dx = where.x - fLockedPos.x;
            float dy = where.y - fLockedPos.y;

            // Drop pure warp-echo events (where == fLockedPos) so the
            // controller doesn't see fake (0,0) deltas that masquerade
            // as real motion in the per-event log.
            const float kWarpEpsilon = 0.5f;
            bool isWarpEcho = (fabsf(dx) < kWarpEpsilon && fabsf(dy) < kWarpEpsilon);

            if (!isWarpEcho && fEventPort >= 0) {
                BMessage evt(SOFTKM_EVT_MOUSE_MOVE);
                evt.AddPoint("where", where);
                evt.AddFloat("dx", dx);
                evt.AddFloat("dy", dy);
                evt.AddInt32("buttons", buttons);
                evt.AddInt32("modifiers", modifiers);

                ssize_t flat = evt.FlattenedSize();
                char* buf2 = new char[flat];
                if (evt.Flatten(buf2, flat) == B_OK)
                    write_port_etc(fEventPort, evt.what, buf2, flat,
                                   B_TIMEOUT, 50000);
                delete[] buf2;
            }

            // Warp cursor back to locked position so it never leaves
            // the edge. The B_MOUSE_MOVED that input_server posts in
            // response to this warp will arrive here with where ==
            // fLockedPos and be identified as a warp echo above.
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

            if (fEventPort >= 0) {
                BMessage evt(SOFTKM_EVT_MOUSE_DOWN);
                evt.AddPoint("where", where);
                evt.AddInt32("buttons", buttons);
                evt.AddInt32("modifiers", modifiers);
                evt.AddInt32("clicks", clicks);

                ssize_t flat = evt.FlattenedSize();
                char* buf2 = new char[flat];
                if (evt.Flatten(buf2, flat) == B_OK)
                    write_port_etc(fEventPort, evt.what, buf2, flat,
                                   B_TIMEOUT, 50000);
                delete[] buf2;
            }
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

            if (fEventPort >= 0) {
                BMessage evt(SOFTKM_EVT_MOUSE_UP);
                evt.AddPoint("where", where);
                evt.AddInt32("buttons", buttons);
                evt.AddInt32("modifiers", modifiers);

                ssize_t flat = evt.FlattenedSize();
                char* buf2 = new char[flat];
                if (evt.Flatten(buf2, flat) == B_OK)
                    write_port_etc(fEventPort, evt.what, buf2, flat,
                                   B_TIMEOUT, 50000);
                delete[] buf2;
            }
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

            if (fEventPort >= 0) {
                BMessage evt(SOFTKM_EVT_MOUSE_WHEEL);
                evt.AddFloat("delta_x", dx);
                evt.AddFloat("delta_y", dy);
                evt.AddInt32("modifiers", modifiers);

                ssize_t flat = evt.FlattenedSize();
                char* buf2 = new char[flat];
                if (evt.Flatten(buf2, flat) == B_OK)
                    write_port_etc(fEventPort, evt.what, buf2, flat,
                                   B_TIMEOUT, 50000);
                delete[] buf2;
            }
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

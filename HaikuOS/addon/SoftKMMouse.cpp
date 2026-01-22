/*
 * SoftKM Mouse Device Add-on
 *
 * This add-on receives mouse events from the main softKM app
 * and injects them into the system via EnqueueMessage().
 */

#include <InputServerDevice.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <OS.h>
#include <Point.h>

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <math.h>

// Debug logging - disabled for performance
// Enable by setting SOFTKM_DEBUG=1
#ifdef SOFTKM_DEBUG
static void DebugLog(const char* fmt, ...) {
    FILE* f = fopen("/boot/home/softKM_mouse.log", "a");
    if (f) {
        time_t now = time(NULL);
        struct tm* tm = localtime(&now);
        fprintf(f, "[%02d:%02d:%02d] ", tm->tm_hour, tm->tm_min, tm->tm_sec);
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fclose(f);
    }
}
#else
#define DebugLog(...) ((void)0)
#endif

// Message codes for communication with main app
enum {
    SOFTKM_INJECT_MOUSE_DOWN = 'sMdn',
    SOFTKM_INJECT_MOUSE_UP = 'sMup',
    SOFTKM_INJECT_MOUSE_MOVE = 'sMmv',
    SOFTKM_INJECT_MOUSE_WHEEL = 'sMwh',
};

static const char* kDeviceName = "SoftKM Mouse";
static const char* kPortName = "softKM_mouse_port";
static const char* kVersion = "1.3.0";  // Full click tracking + pre-click mouse move

class SoftKMMouse : public BInputServerDevice {
public:
    SoftKMMouse();
    virtual ~SoftKMMouse();

    virtual status_t InitCheck();
    virtual status_t Start(const char* device, void* cookie);
    virtual status_t Stop(const char* device, void* cookie);
    virtual status_t Control(const char* device, void* cookie,
                            uint32 code, BMessage* message);

private:
    static int32 _WatcherThread(void* data);
    void _WatchPort();
    void _ProcessMessage(BMessage* msg);

    port_id fPort;
    thread_id fWatcherThread;
    bool fRunning;

    // Click tracking for double-click detection
    bigtime_t fLastClickTime;
    BPoint fLastClickPosition;
    int32 fClickCount;
    int32 fLastClickButtons;
    bigtime_t fClickSpeed;
};


SoftKMMouse::SoftKMMouse()
    : BInputServerDevice(),
      fPort(-1),
      fWatcherThread(-1),
      fRunning(false),
      fLastClickTime(0),
      fLastClickPosition(0, 0),
      fClickCount(0),
      fLastClickButtons(0),
      fClickSpeed(500000)  // Default 500ms, will be updated from system settings
{
    // Get system click speed setting
    if (get_click_speed(&fClickSpeed) != B_OK) {
        fClickSpeed = 500000;  // Fallback to 500ms
    }
    DebugLog("Click speed: %lld microseconds", fClickSpeed);
}

SoftKMMouse::~SoftKMMouse()
{
    fRunning = false;
    if (fPort >= 0) {
        delete_port(fPort);
        fPort = -1;
    }
    if (fWatcherThread >= 0) {
        status_t result;
        wait_for_thread(fWatcherThread, &result);
    }
}

status_t SoftKMMouse::InitCheck()
{
    fprintf(stderr, "SoftKMMouse: Version %s initializing\n", kVersion);
    DebugLog("=== SoftKMMouse v%s initializing ===", kVersion);

    // Create a port for receiving mouse events
    fPort = create_port(100, kPortName);
    if (fPort < 0) {
        fprintf(stderr, "SoftKMMouse: Failed to create port\n");
        return B_ERROR;
    }

    fprintf(stderr, "SoftKMMouse: Created port %d\n", fPort);

    // Register as pointing device
    input_device_ref* devices[2];
    input_device_ref device = {
        (char*)kDeviceName,
        B_POINTING_DEVICE,
        (void*)this
    };
    devices[0] = &device;
    devices[1] = NULL;

    RegisterDevices(devices);
    fprintf(stderr, "SoftKMMouse: Registered device '%s'\n", kDeviceName);

    // Start the watcher thread immediately
    fRunning = true;
    fWatcherThread = spawn_thread(_WatcherThread, "softKM_mouse_watcher",
        B_REAL_TIME_PRIORITY, this);

    if (fWatcherThread >= 0) {
        resume_thread(fWatcherThread);
        fprintf(stderr, "SoftKMMouse: Watcher thread started\n");
    } else {
        fprintf(stderr, "SoftKMMouse: Failed to start watcher thread\n");
        fRunning = false;
        return B_ERROR;
    }

    return B_OK;
}

status_t SoftKMMouse::Start(const char* device, void* cookie)
{
    fprintf(stderr, "SoftKMMouse: Start called for '%s'\n", device ? device : "NULL");

    if (fRunning && fWatcherThread >= 0)
        return B_OK;

    fRunning = true;
    fWatcherThread = spawn_thread(_WatcherThread, "softKM_mouse_watcher",
        B_REAL_TIME_PRIORITY, this);

    if (fWatcherThread < 0) {
        fprintf(stderr, "SoftKMMouse: Failed to spawn watcher thread\n");
        fRunning = false;
        return B_ERROR;
    }

    resume_thread(fWatcherThread);
    return B_OK;
}

status_t SoftKMMouse::Stop(const char* device, void* cookie)
{
    fprintf(stderr, "SoftKMMouse: Stop called\n");

    fRunning = false;
    if (fPort >= 0) {
        write_port(fPort, 0, NULL, 0);
    }

    if (fWatcherThread >= 0) {
        status_t result;
        wait_for_thread(fWatcherThread, &result);
        fWatcherThread = -1;
    }

    return B_OK;
}

status_t SoftKMMouse::Control(const char* device, void* cookie,
    uint32 code, BMessage* message)
{
    return B_OK;
}

int32 SoftKMMouse::_WatcherThread(void* data)
{
    SoftKMMouse* device = (SoftKMMouse*)data;
    device->_WatchPort();
    return 0;
}

void SoftKMMouse::_WatchPort()
{
    while (fRunning) {
        int32 code;
        char buffer[4096];
        ssize_t size = read_port(fPort, &code, buffer, sizeof(buffer));

        if (size < 0) {
            if (size == B_INTERRUPTED)
                continue;
            break;
        }

        if (size == 0 || code == 0)
            continue;

        BMessage msg;
        if (msg.Unflatten(buffer) == B_OK) {
            _ProcessMessage(&msg);
        }
    }
}

void SoftKMMouse::_ProcessMessage(BMessage* msg)
{
    BMessage* event = NULL;

    switch (msg->what) {
        case SOFTKM_INJECT_MOUSE_MOVE:
        {
            BPoint where;
            if (msg->FindPoint("where", &where) == B_OK) {
                event = new BMessage(B_MOUSE_MOVED);
                event->AddInt64("when", system_time());
                event->AddPoint("where", where);
                event->AddInt32("buttons", msg->GetInt32("buttons", 0));
                event->AddInt32("modifiers", msg->GetInt32("modifiers", 0));
            }
            break;
        }

        case SOFTKM_INJECT_MOUSE_DOWN:
        {
            BPoint where;
            if (msg->FindPoint("where", &where) == B_OK) {
                int32 modifiers = msg->GetInt32("modifiers", 0);
                int32 buttons = msg->GetInt32("buttons", 0);
                bigtime_t when = system_time();

                // Track clicks ourselves - system doesn't reliably do it for injected events
                float dx = where.x - fLastClickPosition.x;
                float dy = where.y - fLastClickPosition.y;
                float distance = sqrtf(dx * dx + dy * dy);

                // Check if this is a continuation click (same button, within time & distance)
                if (buttons == fLastClickButtons &&
                    fLastClickTime > 0 &&
                    (when - fLastClickTime) <= fClickSpeed &&
                    distance < 4.0f) {
                    fClickCount++;
                } else {
                    fClickCount = 1;
                }

                // Update ALL tracking state
                fLastClickTime = when;
                fLastClickPosition = where;
                fLastClickButtons = buttons;

                // First send a mouse moved to ensure cursor position is synced
                BMessage* moveEvent = new BMessage(B_MOUSE_MOVED);
                moveEvent->AddInt64("when", when - 1000);  // Slightly before click
                moveEvent->AddPoint("where", where);
                moveEvent->AddInt32("buttons", 0);
                moveEvent->AddInt32("modifiers", modifiers);
                EnqueueMessage(moveEvent);

                // Now send the click
                event = new BMessage(B_MOUSE_DOWN);
                event->AddInt64("when", when);
                event->AddPoint("where", where);
                event->AddInt32("buttons", buttons);
                event->AddInt32("modifiers", modifiers);
                event->AddInt32("clicks", fClickCount);

                DebugLog("MOUSE_DOWN: btns=0x%x clicks=%d at (%.0f,%.0f) dist=%.1f",
                    buttons, fClickCount, where.x, where.y, distance);
            }
            break;
        }

        case SOFTKM_INJECT_MOUSE_UP:
        {
            BPoint where;
            if (msg->FindPoint("where", &where) == B_OK) {
                int32 buttons = msg->GetInt32("buttons", 0);
                int32 modifiers = msg->GetInt32("modifiers", 0);

                // Use current system_time() for consistent timing
                bigtime_t when = system_time();

                event = new BMessage(B_MOUSE_UP);
                event->AddInt64("when", when);
                event->AddPoint("where", where);
                event->AddInt32("buttons", buttons);
                event->AddInt32("modifiers", modifiers);
                DebugLog("MOUSE_UP: btns=0x%x at (%.0f,%.0f) when=%lld",
                    buttons, where.x, where.y, when);
            }
            break;
        }

        case SOFTKM_INJECT_MOUSE_WHEEL:
        {
            float deltaX = msg->GetFloat("delta_x", 0.0f);
            float deltaY = msg->GetFloat("delta_y", 0.0f);
            int32 modifiers = msg->GetInt32("modifiers", 0);

            bigtime_t when = msg->GetInt64("when", system_time());

            event = new BMessage(B_MOUSE_WHEEL_CHANGED);
            event->AddInt64("when", when);
            // Invert deltas - macOS and Haiku have opposite scroll directions
            event->AddFloat("be:wheel_delta_x", -deltaX);
            event->AddFloat("be:wheel_delta_y", -deltaY);
            event->AddInt32("modifiers", modifiers);
            break;
        }
    }

    if (event != NULL) {
        EnqueueMessage(event);
    }
}


extern "C" BInputServerDevice* instantiate_input_device()
{
    return new SoftKMMouse();
}

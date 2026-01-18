/*
 * SoftKM Mouse Device Add-on
 *
 * This add-on receives mouse events from the main softKM app
 * and injects them into the system via EnqueueMessage().
 */

#include <InputServerDevice.h>
#include <Message.h>
#include <OS.h>

#include <stdio.h>

// Message codes for communication with main app
enum {
    SOFTKM_INJECT_MOUSE_DOWN = 'sMdn',
    SOFTKM_INJECT_MOUSE_UP = 'sMup',
    SOFTKM_INJECT_MOUSE_MOVE = 'sMmv',
    SOFTKM_INJECT_MOUSE_WHEEL = 'sMwh',
};

static const char* kDeviceName = "SoftKM Mouse";
static const char* kPortName = "softKM_mouse_port";

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
};


SoftKMMouse::SoftKMMouse()
    : BInputServerDevice(),
      fPort(-1),
      fWatcherThread(-1),
      fRunning(false)
{
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
                event = new BMessage(B_MOUSE_DOWN);
                event->AddInt64("when", system_time());
                event->AddPoint("where", where);
                event->AddInt32("buttons", msg->GetInt32("buttons", 0));
                event->AddInt32("modifiers", modifiers);
                event->AddInt32("clicks", msg->GetInt32("clicks", 1));
                fprintf(stderr, "SoftKMMouse: MOUSE_DOWN at (%.0f,%.0f) btns=0x%x mods=0x%x\n",
                    where.x, where.y, msg->GetInt32("buttons", 0), modifiers);
            }
            break;
        }

        case SOFTKM_INJECT_MOUSE_UP:
        {
            BPoint where;
            if (msg->FindPoint("where", &where) == B_OK) {
                event = new BMessage(B_MOUSE_UP);
                event->AddInt64("when", system_time());
                event->AddPoint("where", where);
                event->AddInt32("buttons", msg->GetInt32("buttons", 0));
                event->AddInt32("modifiers", msg->GetInt32("modifiers", 0));
                fprintf(stderr, "SoftKMMouse: MOUSE_UP at (%.0f,%.0f)\n",
                    where.x, where.y);
            }
            break;
        }

        case SOFTKM_INJECT_MOUSE_WHEEL:
        {
            event = new BMessage(B_MOUSE_WHEEL_CHANGED);
            event->AddInt64("when", system_time());
            event->AddFloat("be:wheel_delta_x", msg->GetFloat("delta_x", 0.0f));
            event->AddFloat("be:wheel_delta_y", msg->GetFloat("delta_y", 0.0f));
            event->AddInt32("modifiers", msg->GetInt32("modifiers", 0));
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

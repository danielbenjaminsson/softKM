/*
 * SoftKM Input Server Device Add-on
 *
 * This add-on receives input events from the main softKM app
 * and injects them into the system via EnqueueMessage().
 */

#include <InputServerDevice.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <OS.h>

#include <stdio.h>

// Message codes for communication with main app
enum {
    SOFTKM_INJECT_MOUSE_DOWN = 'sMdn',
    SOFTKM_INJECT_MOUSE_UP = 'sMup',
    SOFTKM_INJECT_MOUSE_MOVE = 'sMmv',
    SOFTKM_INJECT_MOUSE_WHEEL = 'sMwh',
    SOFTKM_INJECT_KEY_DOWN = 'sKdn',
    SOFTKM_INJECT_KEY_UP = 'sKup',
};

class SoftKMDevice : public BInputServerDevice {
public:
    SoftKMDevice();
    virtual ~SoftKMDevice();

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


SoftKMDevice::SoftKMDevice()
    : BInputServerDevice(),
      fPort(-1),
      fWatcherThread(-1),
      fRunning(false)
{
}

SoftKMDevice::~SoftKMDevice()
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

status_t SoftKMDevice::InitCheck()
{
    // Create a port for receiving events from the main app
    fPort = create_port(100, "softKM_input_port");
    if (fPort < 0) {
        fprintf(stderr, "SoftKMDevice: Failed to create port\n");
        return B_ERROR;
    }

    fprintf(stderr, "SoftKMDevice: Created port %ld\n", fPort);
    return B_OK;
}

status_t SoftKMDevice::Start(const char* device, void* cookie)
{
    fprintf(stderr, "SoftKMDevice: Start called\n");

    fRunning = true;
    fWatcherThread = spawn_thread(_WatcherThread, "softKM_watcher",
        B_REAL_TIME_PRIORITY, this);

    if (fWatcherThread < 0) {
        fRunning = false;
        return B_ERROR;
    }

    resume_thread(fWatcherThread);
    return B_OK;
}

status_t SoftKMDevice::Stop(const char* device, void* cookie)
{
    fprintf(stderr, "SoftKMDevice: Stop called\n");

    fRunning = false;
    if (fPort >= 0) {
        // Write a dummy message to wake up the watcher
        write_port(fPort, 0, NULL, 0);
    }

    if (fWatcherThread >= 0) {
        status_t result;
        wait_for_thread(fWatcherThread, &result);
        fWatcherThread = -1;
    }

    return B_OK;
}

status_t SoftKMDevice::Control(const char* device, void* cookie,
    uint32 code, BMessage* message)
{
    return B_OK;
}

int32 SoftKMDevice::_WatcherThread(void* data)
{
    SoftKMDevice* device = (SoftKMDevice*)data;
    device->_WatchPort();
    return 0;
}

void SoftKMDevice::_WatchPort()
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

        // Unflatten the BMessage
        BMessage msg;
        if (msg.Unflatten(buffer) == B_OK) {
            _ProcessMessage(&msg);
        }
    }
}

void SoftKMDevice::_ProcessMessage(BMessage* msg)
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
            }
            break;
        }

        case SOFTKM_INJECT_MOUSE_DOWN:
        {
            BPoint where;
            if (msg->FindPoint("where", &where) == B_OK) {
                event = new BMessage(B_MOUSE_DOWN);
                event->AddInt64("when", system_time());
                event->AddPoint("where", where);
                event->AddInt32("buttons", msg->GetInt32("buttons", 0));
                event->AddInt32("modifiers", 0);
                event->AddInt32("clicks", msg->GetInt32("clicks", 1));
                fprintf(stderr, "SoftKMDevice: MOUSE_DOWN at (%.0f,%.0f) btns=0x%x\n",
                    where.x, where.y, msg->GetInt32("buttons", 0));
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
                event->AddInt32("modifiers", 0);
                fprintf(stderr, "SoftKMDevice: MOUSE_UP at (%.0f,%.0f)\n",
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
            break;
        }

        case SOFTKM_INJECT_KEY_DOWN:
        {
            event = new BMessage(B_KEY_DOWN);
            event->AddInt64("when", system_time());
            event->AddInt32("key", msg->GetInt32("key", 0));
            event->AddInt32("modifiers", msg->GetInt32("modifiers", 0));
            event->AddInt32("raw_char", msg->GetInt32("raw_char", 0));

            const char* bytes;
            if (msg->FindString("bytes", &bytes) == B_OK) {
                event->AddString("bytes", bytes);
            }
            break;
        }

        case SOFTKM_INJECT_KEY_UP:
        {
            event = new BMessage(B_KEY_UP);
            event->AddInt64("when", system_time());
            event->AddInt32("key", msg->GetInt32("key", 0));
            event->AddInt32("modifiers", msg->GetInt32("modifiers", 0));
            event->AddInt32("raw_char", 0);
            event->AddString("bytes", "");
            break;
        }
    }

    if (event != NULL) {
        // EnqueueMessage takes ownership of the message
        EnqueueMessage(event);
    }
}


// Export functions for input_server to load this add-on
extern "C" BInputServerDevice* instantiate_input_device()
{
    return new SoftKMDevice();
}

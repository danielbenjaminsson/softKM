/*
 * SoftKM Keyboard Device Add-on
 *
 * This add-on receives keyboard events from the main softKM app
 * and injects them into the system via EnqueueMessage().
 */

#include <InputServerDevice.h>
#include <Message.h>
#include <OS.h>

#include <stdio.h>

// Message codes for communication with main app
enum {
    SOFTKM_INJECT_KEY_DOWN = 'sKdn',
    SOFTKM_INJECT_KEY_UP = 'sKup',
};

static const char* kDeviceName = "SoftKM Keyboard";
static const char* kPortName = "softKM_keyboard_port";

class SoftKMKeyboard : public BInputServerDevice {
public:
    SoftKMKeyboard();
    virtual ~SoftKMKeyboard();

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


SoftKMKeyboard::SoftKMKeyboard()
    : BInputServerDevice(),
      fPort(-1),
      fWatcherThread(-1),
      fRunning(false)
{
}

SoftKMKeyboard::~SoftKMKeyboard()
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

status_t SoftKMKeyboard::InitCheck()
{
    // Create a port for receiving keyboard events
    fPort = create_port(100, kPortName);
    if (fPort < 0) {
        fprintf(stderr, "SoftKMKeyboard: Failed to create port\n");
        return B_ERROR;
    }

    fprintf(stderr, "SoftKMKeyboard: Created port %d\n", fPort);

    // Register as keyboard device
    input_device_ref* devices[2];
    input_device_ref device = {
        (char*)kDeviceName,
        B_KEYBOARD_DEVICE,
        (void*)this
    };
    devices[0] = &device;
    devices[1] = NULL;

    RegisterDevices(devices);
    fprintf(stderr, "SoftKMKeyboard: Registered device '%s'\n", kDeviceName);

    // Start the watcher thread immediately
    fRunning = true;
    fWatcherThread = spawn_thread(_WatcherThread, "softKM_keyboard_watcher",
        B_REAL_TIME_PRIORITY, this);

    if (fWatcherThread >= 0) {
        resume_thread(fWatcherThread);
        fprintf(stderr, "SoftKMKeyboard: Watcher thread started\n");
    } else {
        fprintf(stderr, "SoftKMKeyboard: Failed to start watcher thread\n");
        fRunning = false;
        return B_ERROR;
    }

    return B_OK;
}

status_t SoftKMKeyboard::Start(const char* device, void* cookie)
{
    fprintf(stderr, "SoftKMKeyboard: Start called for '%s'\n", device ? device : "NULL");

    if (fRunning && fWatcherThread >= 0)
        return B_OK;

    fRunning = true;
    fWatcherThread = spawn_thread(_WatcherThread, "softKM_keyboard_watcher",
        B_REAL_TIME_PRIORITY, this);

    if (fWatcherThread < 0) {
        fprintf(stderr, "SoftKMKeyboard: Failed to spawn watcher thread\n");
        fRunning = false;
        return B_ERROR;
    }

    resume_thread(fWatcherThread);
    return B_OK;
}

status_t SoftKMKeyboard::Stop(const char* device, void* cookie)
{
    fprintf(stderr, "SoftKMKeyboard: Stop called\n");

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

status_t SoftKMKeyboard::Control(const char* device, void* cookie,
    uint32 code, BMessage* message)
{
    return B_OK;
}

int32 SoftKMKeyboard::_WatcherThread(void* data)
{
    SoftKMKeyboard* device = (SoftKMKeyboard*)data;
    device->_WatchPort();
    return 0;
}

void SoftKMKeyboard::_WatchPort()
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

void SoftKMKeyboard::_ProcessMessage(BMessage* msg)
{
    BMessage* event = NULL;

    switch (msg->what) {
        case SOFTKM_INJECT_KEY_DOWN:
        {
            int32 key = msg->GetInt32("key", 0);
            int32 modifiers = msg->GetInt32("modifiers", 0);
            int32 rawChar = msg->GetInt32("raw_char", 0);

            fprintf(stderr, "SoftKMKeyboard: KEY_DOWN key=0x%02x mods=0x%02x raw=0x%02x\n",
                key, modifiers, rawChar);

            event = new BMessage(B_KEY_DOWN);
            event->AddInt64("when", system_time());
            event->AddInt32("key", key);
            event->AddInt32("modifiers", modifiers);
            event->AddInt32("raw_char", rawChar);

            const char* bytes;
            if (msg->FindString("bytes", &bytes) == B_OK) {
                event->AddString("bytes", bytes);
                event->AddInt8("byte", bytes[0]);
            } else {
                event->AddString("bytes", "");
                event->AddInt8("byte", 0);
            }
            break;
        }

        case SOFTKM_INJECT_KEY_UP:
        {
            int32 key = msg->GetInt32("key", 0);
            int32 modifiers = msg->GetInt32("modifiers", 0);

            fprintf(stderr, "SoftKMKeyboard: KEY_UP key=0x%02x mods=0x%02x\n",
                key, modifiers);

            event = new BMessage(B_KEY_UP);
            event->AddInt64("when", system_time());
            event->AddInt32("key", key);
            event->AddInt32("modifiers", modifiers);
            event->AddInt32("raw_char", 0);
            event->AddString("bytes", "");
            event->AddInt8("byte", 0);
            break;
        }
    }

    if (event != NULL) {
        EnqueueMessage(event);
    }
}


extern "C" BInputServerDevice* instantiate_input_device()
{
    return new SoftKMKeyboard();
}

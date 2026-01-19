/*
 * SoftKM Keyboard Device Add-on
 *
 * This add-on receives keyboard events from the main softKM app
 * and injects them into the system via EnqueueMessage().
 */

#include <InputServerDevice.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <OS.h>

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

// Debug logging to file
static void DebugLog(const char* fmt, ...) {
    FILE* f = fopen("/boot/home/softKM_keyboard.log", "a");
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

// Message codes for communication with main app
enum {
    SOFTKM_INJECT_KEY_DOWN = 'sKdn',
    SOFTKM_INJECT_KEY_UP = 'sKup',
};

// Key state array size (128 keys = 16 bytes)
#define KEY_STATES_SIZE 16

static const char* kDeviceName = "SoftKM Keyboard";
static const char* kPortName = "softKM_keyboard_port";
static const char* kVersion = "1.2.0";  // Synthesize Ctrl+letter from keycode if no bytes

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
    void _SetKeyState(int32 key, bool pressed);

    port_id fPort;
    thread_id fWatcherThread;
    bool fRunning;
    uint8 fKeyStates[KEY_STATES_SIZE];  // Bit array of key states
};


SoftKMKeyboard::SoftKMKeyboard()
    : BInputServerDevice(),
      fPort(-1),
      fWatcherThread(-1),
      fRunning(false)
{
    memset(fKeyStates, 0, sizeof(fKeyStates));
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
    fprintf(stderr, "SoftKMKeyboard: Version %s initializing\n", kVersion);
    DebugLog("=== SoftKMKeyboard v%s initializing ===", kVersion);

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
            DebugLog("Received message: what=0x%08x", (uint32)msg.what);
            _ProcessMessage(&msg);
        }
    }
}

void SoftKMKeyboard::_SetKeyState(int32 key, bool pressed)
{
    if (key < 0 || key >= KEY_STATES_SIZE * 8)
        return;

    int byteIndex = key / 8;
    int bitIndex = key % 8;

    if (pressed) {
        fKeyStates[byteIndex] |= (1 << bitIndex);
    } else {
        fKeyStates[byteIndex] &= ~(1 << bitIndex);
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

            // Update key state
            _SetKeyState(key, true);

            fprintf(stderr, "SoftKMKeyboard: KEY_DOWN key=0x%02x mods=0x%02x raw=0x%02x\n",
                key, modifiers, rawChar);
            DebugLog("KEY_DOWN key=0x%02x mods=0x%02x raw=0x%02x", key, modifiers, rawChar);

            event = new BMessage(B_KEY_DOWN);
            event->AddInt64("when", system_time());
            event->AddInt32("key", key);
            event->AddInt32("modifiers", modifiers);

            // Handle special keys that need specific byte values
            char specialByte = 0;
            const char* specialBytes = NULL;
            static char byteBuffer[2] = {0, 0};

            switch (key) {
                case 0x1e:  // Backspace
                    specialByte = 0x08;
                    break;
                case 0x26:  // Tab
                    specialByte = 0x09;
                    break;
                case 0x47:  // Return/Enter
                    specialByte = 0x0a;
                    break;
                case 0x01:  // Escape
                    specialByte = 0x1b;
                    break;
                case 0x34:  // Delete (forward)
                    specialByte = 0x7f;
                    break;
                case 0x5e:  // Space
                    specialByte = 0x20;
                    break;
                // Arrow keys - use Haiku's B_*_ARROW constants
                case 0x61:  // Left Arrow
                    specialByte = B_LEFT_ARROW;
                    break;
                case 0x63:  // Right Arrow
                    specialByte = B_RIGHT_ARROW;
                    break;
                case 0x62:  // Down Arrow
                    specialByte = B_DOWN_ARROW;
                    break;
                case 0x9e:  // Up Arrow
                    specialByte = B_UP_ARROW;
                    break;
                case 0x20:  // Home
                    specialByte = B_HOME;
                    break;
                case 0x35:  // End
                    specialByte = B_END;
                    break;
                case 0x21:  // Page Up
                    specialByte = B_PAGE_UP;
                    break;
                case 0x36:  // Page Down
                    specialByte = B_PAGE_DOWN;
                    break;
            }

            if (specialByte != 0) {
                byteBuffer[0] = specialByte;
                specialBytes = byteBuffer;
                rawChar = specialByte;
            }

            // Handle Ctrl+letter combinations
            // Haiku apps may expect either the control character (0x01-0x1A) OR
            // the regular letter with B_CONTROL_KEY modifier. We'll provide both:
            // - For terminal-style apps: send control character in bytes
            // - For UI apps: modifiers indicate Ctrl is pressed
            // B_CONTROL_KEY = 0x04, B_LEFT_CONTROL_KEY = 0x2000, B_RIGHT_CONTROL_KEY = 0x4000
            if (specialByte == 0 && (modifiers & 0x04)) {
                DebugLog("Ctrl key detected, checking bytes");
                const char* bytes;
                bool bytesProcessed = false;
                if (msg->FindString("bytes", &bytes) == B_OK && bytes[0] != '\0') {
                    DebugLog("bytes field found: bytes[0]=0x%02x", (uint8)bytes[0]);
                    char ch = bytes[0];
                    // If it's a control character (1-26), convert back to letter
                    // for the raw_char, but keep the control char in bytes
                    if (ch >= 1 && ch <= 26) {
                        // Control character from macOS - pass it through directly
                        // Terminal and other apps expect the actual control character
                        specialByte = ch;
                        byteBuffer[0] = ch;
                        specialBytes = byteBuffer;
                        rawChar = ch;
                        bytesProcessed = true;
                        DebugLog("control char 0x%02x -> passing through", ch);
                    } else if (ch >= 'a' && ch <= 'z') {
                        // Lowercase letter - convert to control character
                        specialByte = ch - 'a' + 1;
                        byteBuffer[0] = specialByte;
                        specialBytes = byteBuffer;
                        rawChar = ch;
                        bytesProcessed = true;
                        DebugLog("letter '%c' -> control char 0x%02x", ch, specialByte);
                    } else if (ch >= 'A' && ch <= 'Z') {
                        // Uppercase letter - convert to control character
                        specialByte = ch - 'A' + 1;
                        byteBuffer[0] = specialByte;
                        specialBytes = byteBuffer;
                        rawChar = ch;
                        bytesProcessed = true;
                        DebugLog("letter '%c' -> control char 0x%02x", ch, specialByte);
                    } else {
                        DebugLog("ch=0x%02x not a letter or control char", (uint8)ch);
                    }
                }

                // If no bytes received, synthesize control character from key code
                // This handles the case where macOS doesn't send character bytes for Ctrl+key
                if (!bytesProcessed) {
                    DebugLog("No bytes or empty, synthesizing from key code 0x%02x", key);
                    // Map Haiku key codes to letters
                    char letter = 0;
                    switch (key) {
                        case 0x3c: letter = 'a'; break;  // A
                        case 0x40: letter = 'b'; break;  // B
                        case 0x52: letter = 'c'; break;  // C
                        case 0x3e: letter = 'd'; break;  // D
                        case 0x2b: letter = 'e'; break;  // E
                        case 0x3d: letter = 'f'; break;  // F
                        case 0x3f: letter = 'g'; break;  // G
                        case 0x4d: letter = 'h'; break;  // H
                        case 0x30: letter = 'i'; break;  // I
                        case 0x31: letter = 'j'; break;  // J
                        case 0x43: letter = 'k'; break;  // K
                        case 0x53: letter = 'l'; break;  // L
                        case 0x58: letter = 'm'; break;  // M
                        case 0x44: letter = 'n'; break;  // N
                        case 0x41: letter = 'o'; break;  // O
                        case 0x42: letter = 'p'; break;  // P
                        case 0x29: letter = 'q'; break;  // Q
                        case 0x2c: letter = 'r'; break;  // R
                        case 0x50: letter = 's'; break;  // S
                        case 0x2d: letter = 't'; break;  // T
                        case 0x2f: letter = 'u'; break;  // U
                        case 0x4e: letter = 'v'; break;  // V
                        case 0x2a: letter = 'w'; break;  // W
                        case 0x51: letter = 'x'; break;  // X
                        case 0x2e: letter = 'y'; break;  // Y
                        case 0x4f: letter = 'z'; break;  // Z
                    }
                    if (letter != 0) {
                        specialByte = letter - 'a' + 1;  // a=1, b=2, ..., z=26
                        byteBuffer[0] = specialByte;
                        specialBytes = byteBuffer;
                        rawChar = letter;
                        DebugLog("Synthesized Ctrl+%c -> control char 0x%02x", letter, specialByte);
                        fprintf(stderr, "SoftKMKeyboard: Synthesized Ctrl+%c -> 0x%02x\n", letter, specialByte);
                    }
                } else if (specialByte != 0) {
                    DebugLog("Set specialBytes to 0x%02x, rawChar=0x%02x", (uint8)specialByte, rawChar);
                    fprintf(stderr, "SoftKMKeyboard: Ctrl+letter -> control char 0x%02x\n", specialByte);
                }
            }

            event->AddInt32("raw_char", rawChar);
            // Don't add be:key_repeat for first key press - only for actual repeats
            // Adding it with value > 0 causes BWindow to treat it as a repeat and ignore it

            if (specialBytes != NULL) {
                event->AddString("bytes", specialBytes);
                event->AddInt8("byte", specialByte);
                DebugLog("Adding special bytes: byte=0x%02x", (uint8)specialByte);
            } else {
                const char* bytes;
                if (msg->FindString("bytes", &bytes) == B_OK && bytes[0] != '\0') {
                    event->AddString("bytes", bytes);
                    event->AddInt8("byte", bytes[0]);
                    fprintf(stderr, "SoftKMKeyboard: using msg bytes='%s' byte=0x%02x\n",
                        bytes, (uint8)bytes[0]);
                } else {
                    event->AddString("bytes", "");
                    event->AddInt8("byte", 0);
                    fprintf(stderr, "SoftKMKeyboard: no bytes in msg, key=0x%02x\n", key);
                }
            }

            // Add key states array - this is what makes it look like a real keyboard
            event->AddData("states", B_UINT8_TYPE, fKeyStates, KEY_STATES_SIZE);
            break;
        }

        case SOFTKM_INJECT_KEY_UP:
        {
            int32 key = msg->GetInt32("key", 0);
            int32 modifiers = msg->GetInt32("modifiers", 0);

            // Update key state
            _SetKeyState(key, false);

            fprintf(stderr, "SoftKMKeyboard: KEY_UP key=0x%02x mods=0x%02x\n",
                key, modifiers);

            event = new BMessage(B_KEY_UP);
            event->AddInt64("when", system_time());
            event->AddInt32("key", key);
            event->AddInt32("modifiers", modifiers);
            event->AddInt32("raw_char", 0);
            event->AddString("bytes", "");
            event->AddInt8("byte", 0);

            // Add key states array
            event->AddData("states", B_UINT8_TYPE, fKeyStates, KEY_STATES_SIZE);
            break;
        }
    }

    if (event != NULL) {
        DebugLog("EnqueueMessage: what=0x%08x", (uint32)event->what);
        EnqueueMessage(event);

        // For modifier keys, also send B_MODIFIERS_CHANGED
        int32 key = msg->GetInt32("key", 0);
        int32 modifiers = msg->GetInt32("modifiers", 0);

        // Check if this is a modifier key (Alt, Control, Shift, Option/Win)
        bool isModifierKey = (key == 0x4b || key == 0x56 ||  // Shift
                              key == 0x5c || key == 0x60 ||  // Control
                              key == 0x5d || key == 0x5f ||  // Alt (Command)
                              key == 0x66 || key == 0x67 ||  // Win (Option)
                              key == 0x3b);                   // Caps Lock

        if (isModifierKey) {
            BMessage* modMsg = new BMessage(B_MODIFIERS_CHANGED);
            modMsg->AddInt64("when", system_time());
            modMsg->AddInt32("modifiers", modifiers);
            modMsg->AddInt32("be:old_modifiers", 0);
            EnqueueMessage(modMsg);
        }
    }
}


extern "C" BInputServerDevice* instantiate_input_device()
{
    return new SoftKMKeyboard();
}

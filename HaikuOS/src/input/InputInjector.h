#ifndef INPUT_INJECTOR_H
#define INPUT_INJECTOR_H

#include <SupportDefs.h>
#include <Point.h>
#include <OS.h>

class BMessage;
class NetworkServer;

class InputInjector {
public:
    InputInjector();
    ~InputInjector();

    void InjectKeyDown(uint32 keyCode, uint32 modifiers,
        const char* bytes, uint8 numBytes);
    void InjectKeyUp(uint32 keyCode, uint32 modifiers);

    void InjectMouseMove(float x, float y, bool relative);
    void InjectMouseDown(uint32 buttons, float x, float y);
    void InjectMouseUp(uint32 buttons, float x, float y);
    void InjectMouseWheel(float deltaX, float deltaY);

    void ProcessEvent(BMessage* message);

    void SetActive(bool active, float yFromBottom = 0.0f);
    bool IsActive() const { return fActive; }

    void SetNetworkServer(NetworkServer* server) { fNetworkServer = server; }
    void SetDwellTime(float seconds) { fDwellTime = (bigtime_t)(seconds * 1000000); }

private:
    uint32 TranslateKeyCode(uint32 macKeyCode);
    void UpdateMousePosition(float x, float y, bool relative);
    bool SendToKeyboardAddon(BMessage* msg);
    bool SendToMouseAddon(BMessage* msg);
    port_id FindKeyboardPort();
    port_id FindMousePort();

    BPoint fMousePosition;
    uint32 fCurrentButtons;
    uint32 fCurrentModifiers;
    bool fActive;
    port_id fKeyboardPort;
    port_id fMousePort;
    NetworkServer* fNetworkServer;
    bigtime_t fEdgeDwellStart;
    bigtime_t fDwellTime;  // configurable dwell time in microseconds
    bool fAtLeftEdge;
};

#endif // INPUT_INJECTOR_H

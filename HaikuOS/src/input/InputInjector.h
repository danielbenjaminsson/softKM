#ifndef INPUT_INJECTOR_H
#define INPUT_INJECTOR_H

#include <SupportDefs.h>
#include <Point.h>
#include <OS.h>

class BMessage;

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

    void SetActive(bool active);
    bool IsActive() const { return fActive; }

private:
    uint32 TranslateKeyCode(uint32 macKeyCode);
    void UpdateMousePosition(float x, float y, bool relative);
    bool SendToAddon(BMessage* msg);
    port_id FindAddonPort();

    BPoint fMousePosition;
    uint32 fCurrentButtons;
    uint32 fCurrentModifiers;
    bool fActive;
    port_id fAddonPort;
};

#endif // INPUT_INJECTOR_H

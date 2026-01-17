#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

#include <Window.h>

class BTextControl;
class BCheckBox;
class BButton;

class SettingsWindow : public BWindow {
public:
    SettingsWindow();
    virtual ~SettingsWindow();

    virtual void MessageReceived(BMessage* message) override;
    virtual bool QuitRequested() override;

private:
    void LoadSettings();
    void SaveSettings();

    BTextControl* fPortControl;
    BCheckBox* fAutoStartCheck;
    BButton* fSaveButton;
    BButton* fCancelButton;
};

enum {
    MSG_SAVE_SETTINGS = 'save',
    MSG_CANCEL_SETTINGS = 'canc'
};

#endif // SETTINGS_WINDOW_H

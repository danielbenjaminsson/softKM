#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

#include <Window.h>

class BTextControl;
class BCheckBox;
class BButton;
class BMenuBar;
class BMenuItem;

class SettingsWindow : public BWindow {
public:
    SettingsWindow();
    virtual ~SettingsWindow();

    virtual void MessageReceived(BMessage* message) override;
    virtual bool QuitRequested() override;
    virtual void MenusBeginning() override;

private:
    void LoadSettings();
    void SaveSettings();
    void ShowAbout();

    BMenuBar* fMenuBar;
    BMenuItem* fLogMenuItem;
    BTextControl* fPortControl;
    BCheckBox* fAutoStartCheck;
    BCheckBox* fGameModeCheck;
    BButton* fSaveButton;
    BButton* fCancelButton;
};

enum {
    MSG_SAVE_SETTINGS = 'save',
    MSG_CANCEL_SETTINGS = 'canc',
    MSG_ABOUT = 'abou',
    MSG_SHOW_LOGS = 'logs'
};

#endif // SETTINGS_WINDOW_H

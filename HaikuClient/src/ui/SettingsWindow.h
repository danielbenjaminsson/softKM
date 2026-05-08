#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

#include <Window.h>
#include <TextControl.h>
#include <CheckBox.h>
#include <Button.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MenuField.h>
#include <Slider.h>

class SettingsWindow : public BWindow {
public:
    SettingsWindow();
    virtual ~SettingsWindow();

    virtual void MessageReceived(BMessage* message);
    virtual bool QuitRequested();
    virtual void MenusBeginning();

private:
    void LoadSettings();
    void SaveSettings();
    void ShowAbout();
    void ApplyAndReconnect();

    enum {
        MSG_SAVE_SETTINGS   = 'wSAV',
        MSG_CANCEL_SETTINGS = 'wCAN',
        MSG_ABOUT           = 'wABT',
        MSG_SHOW_LOGS       = 'wLOG',
    };

    BMenuBar*     fMenuBar;
    BMenuItem*    fLogMenuItem;

    BTextControl* fHostControl;
    BTextControl* fPortControl;
    BMenuField*   fSwitchEdgeField;
    BMenuField*   fReturnEdgeField;
    BSlider*      fDwellSlider;
    BCheckBox*    fAutoStartCheck;

    BButton*      fSaveButton;
    BButton*      fCancelButton;
};

#endif // SETTINGS_WINDOW_H

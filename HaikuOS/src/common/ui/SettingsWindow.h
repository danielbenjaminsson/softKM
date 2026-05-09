#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

#include <Window.h>

class BTextControl;
class BCheckBox;
class BButton;
class BMenuBar;
class BMenuItem;
class BMenuField;
class BSlider;
class BRadioButton;
class BLayoutItem;
class BStringView;
class BSeparatorView;

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

    // Show/hide widgets that only apply to one mode. Driven by the
    // 'modeChanged' message from the radio buttons and once at
    // window construction after LoadSettings().
    void ApplyModeVisibility();

    // ---- Common ----
    BMenuBar*     fMenuBar;
    BMenuItem*    fLogMenuItem;
    BStringView*  fTitle;
    BCheckBox*    fAutoStartCheck;
    BButton*      fSaveButton;
    BButton*      fCancelButton;

    // ---- Mode radio ----
    BRadioButton* fClientRadio;
    BRadioButton* fServerRadio;

    // ---- Client-mode controls ----
    BTextControl* fHostControl;
    BMenuField*   fSwitchEdgeField;
    BMenuField*   fReturnEdgeField;
    BSlider*      fDwellSlider;

    // ---- Common: port (used in both modes; label changes
    //      meaning between 'connect to' and 'listen on') ----
    BTextControl* fPortControl;

    // Layout items we toggle visibility of based on mode.
    // (Each visual line in the form is one item we can hide.)
    BLayoutItem*  fHostRow;
    BLayoutItem*  fSwitchEdgeRow;
    BLayoutItem*  fReturnEdgeRow;
    BLayoutItem*  fDwellRow;
};

enum {
    MSG_SAVE_SETTINGS    = 'save',
    MSG_CANCEL_SETTINGS  = 'canc',
    MSG_ABOUT            = 'abou',
    MSG_SHOW_LOGS        = 'logs',
    MSG_MODE_CHANGED     = 'mode'
};

#endif // SETTINGS_WINDOW_H

#include "SettingsWindow.h"
#include "SoftKMApp.h"
#include "Settings.h"

#include <LayoutBuilder.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <GridLayout.h>
#include <SpaceLayoutItem.h>
#include <TextControl.h>
#include <CheckBox.h>
#include <RadioButton.h>
#include <Button.h>
#include <Slider.h>
#include <MenuField.h>
#include <Menu.h>
#include <MenuItem.h>
#include <MenuBar.h>
#include <StringView.h>
#include <SeparatorView.h>
#include <Alert.h>
#include <private/interface/AboutWindow.h>
#include <AppFileInfo.h>
#include <Application.h>
#include <Roster.h>
#include <File.h>
#include <Messenger.h>

#include <cstdlib>
#include <cstdio>


SettingsWindow::SettingsWindow()
    : BWindow(BRect(100, 100, 480, 420), "softKM Settings",
        B_TITLED_WINDOW,
        B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
    // ---- Menu bar ----
    fMenuBar = new BMenuBar("menubar");
    BMenu* appMenu = new BMenu("softKM");
    appMenu->AddItem(new BMenuItem("About softKM" B_UTF8_ELLIPSIS,
        new BMessage(MSG_ABOUT)));
    appMenu->AddSeparatorItem();
    fLogMenuItem = new BMenuItem("Show Log",
        new BMessage(MSG_SHOW_LOGS), 'L');
    appMenu->AddItem(fLogMenuItem);
    appMenu->AddSeparatorItem();
    appMenu->AddItem(new BMenuItem("Quit",
        new BMessage(B_QUIT_REQUESTED), 'Q'));
    fMenuBar->AddItem(appMenu);

    fTitle = new BStringView("title", "softKM");

    // ---- Mode radio ----
    fClientRadio = new BRadioButton("modeClient",
        "Client (sender — capture this machine's input and send it elsewhere)",
        new BMessage(MSG_MODE_CHANGED));
    fServerRadio = new BRadioButton("modeServer",
        "Server (receiver — listen for input from another machine)",
        new BMessage(MSG_MODE_CHANGED));

    // ---- Common controls ----
    fPortControl    = new BTextControl("Port:", "", nullptr);
    fAutoStartCheck = new BCheckBox("Start automatically on login", nullptr);

    // ---- Client-only controls ----
    fHostControl = new BTextControl("Host:", "", nullptr);

    // Edge popup menus. Order is (right, left, top, bottom) — the
    // index in the menu is what we save in Settings, matching the
    // SwitchEdge enum in Protocol.h.
    BMenu* switchMenu = new BMenu("Switch edge");
    switchMenu->AddItem(new BMenuItem("Right",  nullptr));
    switchMenu->AddItem(new BMenuItem("Left",   nullptr));
    switchMenu->AddItem(new BMenuItem("Top",    nullptr));
    switchMenu->AddItem(new BMenuItem("Bottom", nullptr));
    switchMenu->SetRadioMode(true);

    BMenu* returnMenu = new BMenu("Return edge");
    returnMenu->AddItem(new BMenuItem("Right",  nullptr));
    returnMenu->AddItem(new BMenuItem("Left",   nullptr));
    returnMenu->AddItem(new BMenuItem("Top",    nullptr));
    returnMenu->AddItem(new BMenuItem("Bottom", nullptr));
    returnMenu->SetRadioMode(true);

    fSwitchEdgeField = new BMenuField("switchEdge",
        "Cross to remote on:", switchMenu);
    fReturnEdgeField = new BMenuField("returnEdge",
        "Return on remote's edge:", returnMenu);

    fDwellSlider = new BSlider("dwell", "Edge dwell time:",
        nullptr, 100, 1500, B_HORIZONTAL);
    fDwellSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
    fDwellSlider->SetHashMarkCount(8);
    fDwellSlider->SetLimitLabels("100 ms", "1500 ms");

    fSaveButton   = new BButton("Save",   new BMessage(MSG_SAVE_SETTINGS));
    fCancelButton = new BButton("Cancel", new BMessage(MSG_CANCEL_SETTINGS));

    // ---- Layout ----
    //
    // Build the body group manually (rather than via LayoutBuilder's
    // fluent API) so we can hold onto BLayoutItem* pointers for the
    // rows we want to toggle visibility of when the mode changes.
    BGroupLayout* root = new BGroupLayout(B_VERTICAL, 0);
    SetLayout(root);
    root->AddView(fMenuBar);

    BGroupView* body = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
    body->GroupLayout()->SetInsets(B_USE_WINDOW_INSETS);
    root->AddView(body);

    body->GroupLayout()->AddView(fTitle);
    body->GroupLayout()->AddView(new BSeparatorView(B_HORIZONTAL));

    // Mode selection
    BGroupView* modeGroup = new BGroupView(B_VERTICAL, 0);
    modeGroup->GroupLayout()->AddView(new BStringView("modeLbl", "Mode:"));
    modeGroup->GroupLayout()->AddView(fClientRadio);
    modeGroup->GroupLayout()->AddView(fServerRadio);
    body->GroupLayout()->AddView(modeGroup);

    body->GroupLayout()->AddView(new BSeparatorView(B_HORIZONTAL));

    // Each of these AddView() calls returns a BLayoutItem* that we
    // can later SetVisible() on.
    fHostRow       = body->GroupLayout()->AddView(fHostControl);
    /* port */       body->GroupLayout()->AddView(fPortControl);
    fSwitchEdgeRow = body->GroupLayout()->AddView(fSwitchEdgeField);
    fReturnEdgeRow = body->GroupLayout()->AddView(fReturnEdgeField);
    fDwellRow      = body->GroupLayout()->AddView(fDwellSlider);

    body->GroupLayout()->AddView(fAutoStartCheck);
    body->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
    body->GroupLayout()->AddView(new BSeparatorView(B_HORIZONTAL));

    // Buttons row
    BGroupView* btnRow = new BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
    btnRow->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
    btnRow->GroupLayout()->AddView(fCancelButton);
    btnRow->GroupLayout()->AddView(fSaveButton);
    body->GroupLayout()->AddView(btnRow);

    fSaveButton->MakeDefault(true);

    LoadSettings();
    ApplyModeVisibility();
    CenterOnScreen();
}


SettingsWindow::~SettingsWindow()
{
}


void SettingsWindow::ApplyModeVisibility()
{
    bool isClient = fClientRadio->Value() == B_CONTROL_ON;
    fHostRow      ->SetVisible(isClient);
    fSwitchEdgeRow->SetVisible(isClient);
    fReturnEdgeRow->SetVisible(isClient);
    fDwellRow     ->SetVisible(isClient);

    fTitle->SetText(isClient
        ? "softKM — Client (input sender)"
        : "softKM — Server (input receiver)");

    // The Port label means different things in each mode; rename it.
    fPortControl->SetLabel(isClient ? "Remote port:" : "Listen port:");
}


void SettingsWindow::MenusBeginning()
{
    BWindow::MenusBeginning();
    BMessenger messenger("application/x-vnd.softKM");
    if (messenger.IsValid()) {
        BMessage query(MSG_QUERY_LOG_VISIBLE);
        BMessage reply;
        if (messenger.SendMessage(&query, &reply, 500000, 500000) == B_OK) {
            bool visible = false;
            if (reply.FindBool("visible", &visible) == B_OK)
                fLogMenuItem->SetLabel(visible ? "Hide Log" : "Show Log");
        }
    }
}


void SettingsWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_ABOUT:
            ShowAbout();
            break;
        case MSG_SHOW_LOGS:
            be_app->PostMessage(MSG_TOGGLE_LOG);
            break;
        case MSG_MODE_CHANGED:
            ApplyModeVisibility();
            break;
        case MSG_SAVE_SETTINGS:
            SaveSettings();
            Hide();
            break;
        case MSG_CANCEL_SETTINGS:
            LoadSettings();
            ApplyModeVisibility();
            Hide();
            break;
        default:
            BWindow::MessageReceived(message);
    }
}


bool SettingsWindow::QuitRequested()
{
    Hide();
    return false;  // Just hide — don't actually destroy
}


void SettingsWindow::LoadSettings()
{
    // Mode radio
    bool isClient = (Settings::GetMode() == MODE_CLIENT);
    fClientRadio->SetValue(isClient ? B_CONTROL_ON : B_CONTROL_OFF);
    fServerRadio->SetValue(isClient ? B_CONTROL_OFF : B_CONTROL_ON);

    // Common
    char portBuf[16];
    snprintf(portBuf, sizeof(portBuf), "%u", Settings::GetPort());
    fPortControl->SetText(portBuf);

    fAutoStartCheck->SetValue(
        Settings::GetAutoStart() ? B_CONTROL_ON : B_CONTROL_OFF);

    // Client-only
    fHostControl->SetText(Settings::GetHostAddress());

    BMenu* sm = fSwitchEdgeField->Menu();
    uint8 se = Settings::GetSwitchEdge();
    if (se < 4 && sm->ItemAt(se)) sm->ItemAt(se)->SetMarked(true);

    BMenu* rm = fReturnEdgeField->Menu();
    uint8 re = Settings::GetReturnEdge();
    if (re < 4 && rm->ItemAt(re)) rm->ItemAt(re)->SetMarked(true);

    int32 ms = (int32)(Settings::GetDwellTime() * 1000.0f);
    if (ms < 100)  ms = 100;
    if (ms > 1500) ms = 1500;
    fDwellSlider->SetValue(ms);
}


void SettingsWindow::SaveSettings()
{
    AppMode oldMode = Settings::GetMode();

    // Mode
    AppMode mode = (fClientRadio->Value() == B_CONTROL_ON)
        ? MODE_CLIENT : MODE_SERVER;
    Settings::SetMode(mode);

    // Common
    uint16 port = (uint16)atoi(fPortControl->Text());
    if (port == 0) port = 31337;
    Settings::SetPort(port);

    Settings::SetAutoStart(
        fAutoStartCheck->Value() == B_CONTROL_ON);

    // Client-only — saved unconditionally so they survive a flip
    // back to client mode without being lost.
    Settings::SetHostAddress(fHostControl->Text());

    BMenu* sm = fSwitchEdgeField->Menu();
    for (int32 i = 0; i < sm->CountItems(); i++) {
        if (sm->ItemAt(i)->IsMarked()) {
            Settings::SetSwitchEdge((uint8)i);
            break;
        }
    }
    BMenu* rm = fReturnEdgeField->Menu();
    for (int32 i = 0; i < rm->CountItems(); i++) {
        if (rm->ItemAt(i)->IsMarked()) {
            Settings::SetReturnEdge((uint8)i);
            break;
        }
    }

    Settings::SetDwellTime(fDwellSlider->Value() / 1000.0f);

    Settings::Save();

    if (mode != oldMode) {
        // Mode change requires an app restart to construct the
        // right object set. Inform the user.
        BAlert* alert = new BAlert("softKM",
            "Mode change saved. Quit and relaunch softKM for the "
            "new mode to take effect.",
            "OK", nullptr, nullptr,
            B_WIDTH_AS_USUAL, B_INFO_ALERT);
        alert->Go(nullptr);
    }
}


void SettingsWindow::ShowAbout()
{
    const char* authors[] = {
        "Daniel Benjaminsson",
        nullptr
    };

    BAboutWindow* about = new BAboutWindow("softKM",
        "application/x-vnd.softKM");
    about->AddDescription(
        "Software Keyboard/Mouse Switch for Haiku\n\n"
        "Share keyboard and mouse input between two computers over "
        "the network. One side runs in Client mode (captures input "
        "and forwards it); the other side runs in Server mode "
        "(receives and injects).\n\n"
        "Move your mouse to the configured screen edge to seamlessly "
        "transfer control between machines.");
    about->AddCopyright(2025, "Microgeni AB");
    about->AddAuthors(authors);
    about->Show();
}

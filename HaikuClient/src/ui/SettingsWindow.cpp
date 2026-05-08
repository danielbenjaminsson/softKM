#include "SettingsWindow.h"
#include "../SoftKMClientApp.h"
#include "../settings/Settings.h"
#include "../network/NetworkClient.h"
#include "../input/SwitchController.h"

#include <LayoutBuilder.h>
#include <StringView.h>
#include <SeparatorView.h>
#include <Menu.h>
#include <Messenger.h>
#include <Application.h>
#include <Roster.h>
#include <File.h>
#include <AppFileInfo.h>
#include <private/interface/AboutWindow.h>

#include <cstdlib>
#include <cstdio>

SettingsWindow::SettingsWindow()
    : BWindow(BRect(100, 100, 480, 380), "softKMClient Settings",
        B_TITLED_WINDOW,
        B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
    // ---- Menu bar ----
    fMenuBar = new BMenuBar("menubar");
    BMenu* appMenu = new BMenu("softKMClient");
    appMenu->AddItem(new BMenuItem("About softKMClient" B_UTF8_ELLIPSIS,
        new BMessage(MSG_ABOUT)));
    appMenu->AddSeparatorItem();
    fLogMenuItem = new BMenuItem("Show Log", new BMessage(MSG_SHOW_LOGS), 'L');
    appMenu->AddItem(fLogMenuItem);
    appMenu->AddSeparatorItem();
    appMenu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
    fMenuBar->AddItem(appMenu);

    // ---- Connection ----
    fHostControl = new BTextControl("Host:", "", nullptr);
    fPortControl = new BTextControl("Port:", "", nullptr);

    // ---- Switch edges ----
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
        "Switch to right on:", switchMenu);
    fReturnEdgeField = new BMenuField("returnEdge",
        "Return to left on:", returnMenu);

    // ---- Dwell time slider (100 ms – 1500 ms) ----
    fDwellSlider = new BSlider("dwell", "Edge dwell time:",
        nullptr, 100, 1500, B_HORIZONTAL);
    fDwellSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
    fDwellSlider->SetHashMarkCount(8);
    fDwellSlider->SetLimitLabels("100 ms", "1500 ms");

    fAutoStartCheck = new BCheckBox("Start automatically on login", nullptr);

    fSaveButton   = new BButton("Save",   new BMessage(MSG_SAVE_SETTINGS));
    fCancelButton = new BButton("Cancel", new BMessage(MSG_CANCEL_SETTINGS));

    // ---- Layout ----
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .Add(fMenuBar)
        .AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
            .SetInsets(B_USE_WINDOW_INSETS)
            .AddGroup(B_HORIZONTAL)
                .Add(new BStringView("title", "softKMClient — Haiku sender"))
            .End()
            .Add(new BSeparatorView(B_HORIZONTAL))
            .AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
                .Add(new BStringView("hostLbl",   "Right Haiku host:"), 0, 0)
                .Add(fHostControl,                                       1, 0)
                .Add(new BStringView("portLbl",   "Port:"),             0, 1)
                .Add(fPortControl,                                       1, 1)
            .End()
            .Add(new BSeparatorView(B_HORIZONTAL))
            .Add(fSwitchEdgeField)
            .Add(fReturnEdgeField)
            .Add(fDwellSlider)
            .Add(fAutoStartCheck)
            .AddGlue()
            .Add(new BSeparatorView(B_HORIZONTAL))
            .AddGroup(B_HORIZONTAL)
                .AddGlue()
                .Add(fCancelButton)
                .Add(fSaveButton)
            .End()
        .End()
    .End();

    fSaveButton->MakeDefault(true);
    LoadSettings();
    CenterOnScreen();
}

SettingsWindow::~SettingsWindow()
{
}

void SettingsWindow::MenusBeginning()
{
    BWindow::MenusBeginning();
    BMessenger app("application/x-vnd.softKMClient");
    if (app.IsValid()) {
        BMessage q(MSG_TOGGLE_LOG);  // just to query visibility
        // We'll keep it simple: always say "Show Log"
        // (a proper implementation would query MSG_QUERY_LOG_VISIBLE)
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
        case MSG_SAVE_SETTINGS:
            SaveSettings();
            Hide();
            break;
        case MSG_CANCEL_SETTINGS:
            LoadSettings();
            Hide();
            break;
        default:
            BWindow::MessageReceived(message);
    }
}

bool SettingsWindow::QuitRequested()
{
    Hide();
    return false;
}

void SettingsWindow::LoadSettings()
{
    fHostControl->SetText(Settings::GetHostAddress());

    char portBuf[16];
    snprintf(portBuf, sizeof(portBuf), "%u", Settings::GetPort());
    fPortControl->SetText(portBuf);

    // Switch edge
    BMenu* sm = fSwitchEdgeField->Menu();
    uint8 se = Settings::GetSwitchEdge();
    if (se < 4 && sm->ItemAt(se))
        sm->ItemAt(se)->SetMarked(true);

    // Return edge
    BMenu* rm = fReturnEdgeField->Menu();
    uint8 re = Settings::GetReturnEdge();
    if (re < 4 && rm->ItemAt(re))
        rm->ItemAt(re)->SetMarked(true);

    // Dwell time (seconds → ms for slider)
    int32 ms = (int32)(Settings::GetDwellTime() * 1000.0f);
    if (ms < 100)  ms = 100;
    if (ms > 1500) ms = 1500;
    fDwellSlider->SetValue(ms);

    fAutoStartCheck->SetValue(
        Settings::GetAutoStart() ? B_CONTROL_ON : B_CONTROL_OFF);
}

void SettingsWindow::SaveSettings()
{
    Settings::SetHostAddress(fHostControl->Text());

    uint16 port = (uint16)atoi(fPortControl->Text());
    if (port == 0) port = 31337;
    Settings::SetPort(port);

    // Switch edge
    BMenu* sm = fSwitchEdgeField->Menu();
    for (int32 i = 0; i < sm->CountItems(); i++) {
        if (sm->ItemAt(i)->IsMarked()) {
            Settings::SetSwitchEdge((uint8)i);
            break;
        }
    }

    // Return edge
    BMenu* rm = fReturnEdgeField->Menu();
    for (int32 i = 0; i < rm->CountItems(); i++) {
        if (rm->ItemAt(i)->IsMarked()) {
            Settings::SetReturnEdge((uint8)i);
            break;
        }
    }

    // Dwell time (ms → seconds)
    float dwell = fDwellSlider->Value() / 1000.0f;
    Settings::SetDwellTime(dwell);

    Settings::SetAutoStart(
        fAutoStartCheck->Value() == B_CONTROL_ON);

    Settings::Save();

    // Apply live to running components
    ApplyAndReconnect();
}

void SettingsWindow::ApplyAndReconnect()
{
    SoftKMClientApp* app = SoftKMClientApp::Instance();
    if (!app) return;

    // Update switch controller settings
    // (accessed through the app's internal pointer — quick & simple approach)
    // We just send a "reconnect" message which the app handles
    BMessage msg('rCON');
    BMessenger(be_app).SendMessage(&msg);
}

void SettingsWindow::ShowAbout()
{
    const char* authors[] = { "Daniel Benjaminsson", nullptr };
    BAboutWindow* about = new BAboutWindow("softKMClient",
        "application/x-vnd.softKMClient");
    about->AddDescription(
        "Haiku → Haiku keyboard/mouse sender.\n\n"
        "Captures input on this (left) Haiku and forwards it to the "
        "softKM server running on the right Haiku machine.");
    about->AddCopyright(2025, "Microgeni AB");
    about->AddAuthors(authors);
    about->Show();
}

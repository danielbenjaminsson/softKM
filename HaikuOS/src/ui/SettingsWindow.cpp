#include "SettingsWindow.h"
#include "../SoftKMApp.h"
#include "../settings/Settings.h"

#include <LayoutBuilder.h>
#include <TextControl.h>
#include <CheckBox.h>
#include <Button.h>
#include <StringView.h>
#include <SeparatorView.h>
#include <MenuBar.h>
#include <Menu.h>
#include <MenuItem.h>
#include <private/interface/AboutWindow.h>
#include <AppFileInfo.h>
#include <Application.h>
#include <Roster.h>
#include <File.h>

#include <cstdlib>
#include <cstdio>

SettingsWindow::SettingsWindow()
    : BWindow(BRect(100, 100, 400, 320), "softKM Settings",
        B_TITLED_WINDOW,
        B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
    // Create menu bar
    fMenuBar = new BMenuBar("menubar");
    BMenu* appMenu = new BMenu("softKM");
    appMenu->AddItem(new BMenuItem("About softKM" B_UTF8_ELLIPSIS,
        new BMessage(MSG_ABOUT)));
    appMenu->AddSeparatorItem();
    appMenu->AddItem(new BMenuItem("Show Logs", new BMessage(MSG_SHOW_LOGS), 'L'));
    appMenu->AddSeparatorItem();
    appMenu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q'));
    fMenuBar->AddItem(appMenu);

    fPortControl = new BTextControl("Port:", "", nullptr);
    fPortControl->SetModificationMessage(new BMessage('port'));

    fAutoStartCheck = new BCheckBox("Start automatically on login", nullptr);

    fSaveButton = new BButton("Save", new BMessage(MSG_SAVE_SETTINGS));
    fCancelButton = new BButton("Cancel", new BMessage(MSG_CANCEL_SETTINGS));

    BStringView* statusLabel = new BStringView("status", "Status:");
    BStringView* statusValue = new BStringView("statusValue", "Waiting for connection...");

    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .Add(fMenuBar)
        .AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
            .SetInsets(B_USE_WINDOW_INSETS)
            .AddGroup(B_HORIZONTAL)
                .Add(new BStringView("title", "softKM Server Settings"))
            .End()
            .Add(new BSeparatorView(B_HORIZONTAL))
            .AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
                .Add(new BStringView("portLabel", "Listen Port:"), 0, 0)
                .Add(fPortControl, 1, 0)
                .Add(statusLabel, 0, 1)
                .Add(statusValue, 1, 1)
            .End()
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
            LoadSettings();  // Revert changes
            Hide();
            break;

        default:
            BWindow::MessageReceived(message);
            break;
    }
}

bool SettingsWindow::QuitRequested()
{
    Hide();
    return false;  // Don't actually close, just hide
}

void SettingsWindow::LoadSettings()
{
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%u", Settings::GetPort());
    fPortControl->SetText(portStr);

    fAutoStartCheck->SetValue(Settings::GetAutoStart() ? B_CONTROL_ON : B_CONTROL_OFF);
}

void SettingsWindow::SaveSettings()
{
    uint16 port = (uint16)atoi(fPortControl->Text());
    if (port == 0) {
        port = 31337;  // Default port (leet!)
    }
    Settings::SetPort(port);

    Settings::SetAutoStart(fAutoStartCheck->Value() == B_CONTROL_ON);

    Settings::Save();
}

void SettingsWindow::ShowAbout()
{
    const char* authors[] = {
        "Daniel Benjaminsson (alias dodo75)",
        nullptr
    };

    BAboutWindow* about = new BAboutWindow("softKM", "application/x-vnd.softKM");
    about->AddDescription(
        "Software Keyboard/Mouse Switch for Haiku\n\n"
        "Share keyboard and mouse input between macOS and Haiku OS "
        "computers over a network.\n\n"
        "Move your mouse to the screen edge to seamlessly switch "
        "control between computers.");
    about->AddCopyright(2025, "Microgeni AB");
    about->AddAuthors(authors);
    about->Show();
}

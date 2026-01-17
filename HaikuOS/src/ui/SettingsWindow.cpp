#include "SettingsWindow.h"
#include "../settings/Settings.h"

#include <LayoutBuilder.h>
#include <TextControl.h>
#include <CheckBox.h>
#include <Button.h>
#include <StringView.h>
#include <SeparatorView.h>

#include <cstdlib>
#include <cstdio>

SettingsWindow::SettingsWindow()
    : BWindow(BRect(100, 100, 400, 280), "softKM Settings",
        B_TITLED_WINDOW,
        B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
    fPortControl = new BTextControl("Port:", "", nullptr);
    fPortControl->SetModificationMessage(new BMessage('port'));

    fAutoStartCheck = new BCheckBox("Start automatically on login", nullptr);

    fSaveButton = new BButton("Save", new BMessage(MSG_SAVE_SETTINGS));
    fCancelButton = new BButton("Cancel", new BMessage(MSG_CANCEL_SETTINGS));

    BStringView* statusLabel = new BStringView("status", "Status:");
    BStringView* statusValue = new BStringView("statusValue", "Waiting for connection...");

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
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
        port = 24800;  // Default port
    }
    Settings::SetPort(port);

    Settings::SetAutoStart(fAutoStartCheck->Value() == B_CONTROL_ON);

    Settings::Save();
}

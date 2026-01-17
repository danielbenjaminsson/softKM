#include "SoftKMApp.h"
#include "ui/DeskbarReplicant.h"
#include "ui/SettingsWindow.h"
#include "network/NetworkServer.h"
#include "input/InputInjector.h"
#include "settings/Settings.h"

#include <Deskbar.h>
#include <Roster.h>
#include <Alert.h>

SoftKMApp* SoftKMApp::sInstance = nullptr;

SoftKMApp::SoftKMApp()
    : BApplication("application/x-vnd.softKM"),
      fNetworkServer(nullptr),
      fInputInjector(nullptr),
      fSettingsWindow(nullptr),
      fClientConnected(false)
{
    sInstance = this;

    // Load settings
    Settings::Load();

    // Create input injector
    fInputInjector = new InputInjector();

    // Create network server
    fNetworkServer = new NetworkServer(Settings::GetPort(), fInputInjector);
}

SoftKMApp::~SoftKMApp()
{
    RemoveDeskbarReplicant();

    delete fNetworkServer;
    delete fInputInjector;

    Settings::Save();
    sInstance = nullptr;
}

void SoftKMApp::ReadyToRun()
{
    // Start the network server
    status_t result = fNetworkServer->Start();
    if (result != B_OK) {
        BAlert* alert = new BAlert("Error",
            "Failed to start network server. Check if the port is available.",
            "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
        alert->Go();
    }

    // Install Deskbar replicant
    InstallDeskbarReplicant();
}

void SoftKMApp::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_SHOW_SETTINGS:
            ShowSettingsWindow();
            break;

        case MSG_CLIENT_CONNECTED:
            SetClientConnected(true);
            break;

        case MSG_CLIENT_DISCONNECTED:
            SetClientConnected(false);
            break;

        case MSG_INPUT_EVENT:
            if (fInputInjector != nullptr) {
                fInputInjector->ProcessEvent(message);
            }
            break;

        case MSG_INSTALL_REPLICANT:
            InstallDeskbarReplicant();
            break;

        case MSG_QUIT_REQUESTED:
            PostMessage(B_QUIT_REQUESTED);
            break;

        default:
            BApplication::MessageReceived(message);
            break;
    }
}

bool SoftKMApp::QuitRequested()
{
    fNetworkServer->Stop();
    RemoveDeskbarReplicant();
    return true;
}

void SoftKMApp::SetClientConnected(bool connected)
{
    fClientConnected = connected;

    // Notify Deskbar replicant of status change
    BDeskbar deskbar;
    if (deskbar.HasItem(REPLICANT_NAME)) {
        BMessage statusMsg(MSG_CONNECTION_STATUS);
        statusMsg.AddBool("connected", connected);

        BMessenger messenger(REPLICANT_NAME, -1, nullptr);
        if (messenger.IsValid()) {
            messenger.SendMessage(&statusMsg);
        }
    }
}

void SoftKMApp::InstallDeskbarReplicant()
{
    BDeskbar deskbar;

    // Remove existing replicant if present
    if (deskbar.HasItem(REPLICANT_NAME)) {
        deskbar.RemoveItem(REPLICANT_NAME);
    }

    // Create and install the replicant
    DeskbarReplicant* replicant = new DeskbarReplicant(
        BRect(0, 0, 15, 15), REPLICANT_NAME);

    status_t result = deskbar.AddItem(replicant);
    delete replicant;

    if (result != B_OK) {
        fprintf(stderr, "Failed to install Deskbar replicant: %s\n",
            strerror(result));
    }
}

void SoftKMApp::RemoveDeskbarReplicant()
{
    BDeskbar deskbar;
    if (deskbar.HasItem(REPLICANT_NAME)) {
        deskbar.RemoveItem(REPLICANT_NAME);
    }
}

void SoftKMApp::ShowSettingsWindow()
{
    if (fSettingsWindow == nullptr) {
        fSettingsWindow = new SettingsWindow();
    }

    if (fSettingsWindow->IsHidden()) {
        fSettingsWindow->Show();
    } else {
        fSettingsWindow->Activate();
    }
}

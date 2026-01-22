#include "SoftKMApp.h"
#include "ui/DeskbarReplicant.h"
#include "ui/SettingsWindow.h"
#include "ui/LogWindow.h"
#include "network/NetworkServer.h"
#include "input/InputInjector.h"
#include "clipboard/ClipboardManager.h"
#include "settings/Settings.h"
#include "Logger.h"

#include <cstdio>  // For fprintf, stderr

#include <Deskbar.h>
#include <Roster.h>
#include <Alert.h>

SoftKMApp* SoftKMApp::sInstance = nullptr;

SoftKMApp::SoftKMApp()
    : BApplication("application/x-vnd.softKM"),
      fNetworkServer(nullptr),
      fInputInjector(nullptr),
      fClipboardManager(nullptr),
      fSettingsWindow(nullptr),
      fLogWindow(nullptr),
      fClientConnected(false)
{
    sInstance = this;

    // Load settings
    Settings::Load();

    // Create log window (user can open it from menu)
    fLogWindow = LogWindow::GetInstance();

    // Set up logger to send to log window
    Logger::Instance().SetLogWindow(BMessenger(fLogWindow));

    // Create input injector
    fInputInjector = new InputInjector();

    // Create clipboard manager
    fClipboardManager = new ClipboardManager();

    // Create network server
    fNetworkServer = new NetworkServer(Settings::GetPort(), fInputInjector);

    // Connect injector to server for edge switching
    fInputInjector->SetNetworkServer(fNetworkServer);

    // Connect clipboard manager to server for clipboard sync
    fNetworkServer->SetClipboardManager(fClipboardManager);
}

SoftKMApp::~SoftKMApp()
{
    RemoveDeskbarReplicant();

    delete fNetworkServer;
    delete fInputInjector;
    delete fClipboardManager;

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

        case MSG_SHOW_LOG:
            ShowLogWindow();
            break;

        case MSG_TOGGLE_LOG:
            if (fLogWindow != nullptr) {
                if (fLogWindow->IsHidden()) {
                    fLogWindow->Show();
                } else {
                    fLogWindow->Hide();
                }
            }
            break;

        case MSG_QUERY_LOG_VISIBLE:
        {
            BMessage reply(B_REPLY);
            bool visible = (fLogWindow != nullptr && !fLogWindow->IsHidden());
            reply.AddBool("visible", visible);
            message->SendReply(&reply);
            break;
        }

        case MSG_QUERY_CONNECTION_STATUS:
        {
            BMessage reply(B_REPLY);
            reply.AddBool("connected", fClientConnected);
            message->SendReply(&reply);
            break;
        }

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
    // If we're currently capturing input, give control back to macOS first
    if (fInputInjector != nullptr && fInputInjector->IsActive()) {
        LOG("Returning control to macOS before quitting...");
        fNetworkServer->SendControlSwitch(1, 0.5f);  // 1 = toMac, 0.5 = center
        fInputInjector->SetActive(false);
    }

    fNetworkServer->Stop();
    RemoveDeskbarReplicant();
    return true;
}

void SoftKMApp::SetClientConnected(bool connected)
{
    fClientConnected = connected;
    // Status will be queried by deskbar replicant via MSG_QUERY_CONNECTION_STATUS
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

void SoftKMApp::ShowLogWindow()
{
    if (fLogWindow == nullptr) {
        fLogWindow = LogWindow::GetInstance();
    }

    if (fLogWindow->IsHidden()) {
        fLogWindow->Show();
    } else {
        fLogWindow->Activate();
    }
}

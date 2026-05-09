#include "SoftKMClientApp.h"
#include "ui/DeskbarReplicant.h"
#include "ui/SettingsWindow.h"
#include "ui/LogWindow.h"
#include "network/NetworkClient.h"
#include "input/SwitchController.h"
#include "clipboard/ClipboardManager.h"
#include "settings/Settings.h"
#include "Logger.h"

#include <Deskbar.h>
#include <Alert.h>
#include <AppFileInfo.h>
#include <Roster.h>
#include <File.h>
#include <private/interface/AboutWindow.h>

#include <cstdio>

SoftKMClientApp* SoftKMClientApp::sInstance = nullptr;

// ------------------------------------------------------------------
// Connection callback (called from NetworkClient thread)
// ------------------------------------------------------------------
static void OnConnectionChanged(bool connected, void* cookie)
{
    // Forward to app message loop (thread-safe)
    uint32 what = connected ? MSG_PEER_CONNECTED : MSG_PEER_DISCONNECTED;
    BMessenger(be_app).SendMessage(what);
}

// ------------------------------------------------------------------
SoftKMClientApp::SoftKMClientApp()
    : BApplication("application/x-vnd.softKMClient"),
      fClient(nullptr),
      fSwitchController(nullptr),
      fClipboardManager(nullptr),
      fSettingsWindow(nullptr),
      fLogWindow(nullptr),
      fConnected(false)
{
    sInstance = this;

    Settings::Load();

    fLogWindow = LogWindow::GetInstance();
    Logger::Instance().SetLogWindow(BMessenger(fLogWindow));
    Logger::Instance().Open("/boot/home/softKMClient.log");

    fClipboardManager  = new ClipboardManager();
    fClient            = new NetworkClient();
    fSwitchController  = new SwitchController();

    fClient->SetClipboardManager(fClipboardManager);
    fClient->SetConnectionCallback(OnConnectionChanged, nullptr);

    fSwitchController->SetNetworkClient(fClient);
    fSwitchController->SetClipboardManager(fClipboardManager);
    fSwitchController->SetSwitchEdge(Settings::GetSwitchEdge());
    fSwitchController->SetReturnEdge(Settings::GetReturnEdge());
    fSwitchController->SetDwellTime(Settings::GetDwellTime());
}

SoftKMClientApp::~SoftKMClientApp()
{
    RemoveDeskbarReplicant();

    fSwitchController->Stop();
    fClient->Disconnect();

    delete fSwitchController;
    delete fClient;
    delete fClipboardManager;

    Settings::Save();
    sInstance = nullptr;
}

void SoftKMClientApp::ReadyToRun()
{
    // Start event capture infrastructure
    status_t err = fSwitchController->Start();
    if (err != B_OK) {
        LOG("Failed to start SwitchController: %s", strerror(err));
    }

    // Connect to right Haiku
    fClient->Connect(Settings::GetHostAddress(), Settings::GetPort());

    InstallDeskbarReplicant();
}

bool SoftKMClientApp::IsCapturing() const
{
    return fSwitchController && fSwitchController->IsCapturing();
}

void SoftKMClientApp::SetConnected(bool v)
{
    fConnected = v;
    LOG("Connection state: %s", v ? "CONNECTED" : "DISCONNECTED");
    // Replicant polls MSG_QUERY_STATUS so no explicit invalidate needed
}

void SoftKMClientApp::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_PEER_CONNECTED:
            SetConnected(true);
            // Sync settings to right Haiku
            fClient->SendSettingsSync(
                Settings::GetDwellTime(),
                Settings::GetSwitchEdge(),
                Settings::GetReturnEdge(),
                0.0f);
            break;

        case MSG_PEER_DISCONNECTED:
            SetConnected(false);
            // If we were capturing, forcibly deactivate
            if (fSwitchController->IsCapturing())
                fSwitchController->OnReturnFromRight(0.5f);
            break;

        case MSG_SWITCH_TO_LEFT:
        {
            float yRatio = 0.5f;
            message->FindFloat("yRatio", &yRatio);
            fSwitchController->OnReturnFromRight(yRatio);
            break;
        }

        case MSG_CAPTURE_ACTIVATED:
            // UI update — replicant will re-query
            break;

        case MSG_CAPTURE_DEACTIVATED:
            // UI update
            break;

        case MSG_SHOW_SETTINGS:
            ShowSettingsWindow();
            break;

        case MSG_SHOW_LOG:
        case MSG_TOGGLE_LOG:
            if (fLogWindow) {
                if (fLogWindow->IsHidden()) fLogWindow->Show();
                else                         fLogWindow->Hide();
            }
            break;

        case 'logM':
            if (fLogWindow)
                fLogWindow->PostMessage(message);
            break;

        case MSG_QUERY_STATUS:
        {
            BMessage reply(B_REPLY);
            reply.AddBool("connected",  fConnected);
            reply.AddBool("capturing",  IsCapturing());
            message->SendReply(&reply);
            break;
        }

        case MSG_SHOW_ABOUT:
            ShowAbout();
            break;

        case MSG_INSTALL_REPLICANT:
            InstallDeskbarReplicant();
            break;

        case 'rCON':
        {
            // Settings changed — update live components and reconnect
            fSwitchController->SetSwitchEdge(Settings::GetSwitchEdge());
            fSwitchController->SetReturnEdge(Settings::GetReturnEdge());
            fSwitchController->SetDwellTime(Settings::GetDwellTime());
            fClient->Disconnect();
            fClient->Connect(Settings::GetHostAddress(), Settings::GetPort());
            break;
        }

        case MSG_QUIT_REQUESTED:
            PostMessage(B_QUIT_REQUESTED);
            break;

        default:
            BApplication::MessageReceived(message);
            break;
    }
}

bool SoftKMClientApp::QuitRequested()
{
    if (fSwitchController->IsCapturing()) {
        LOG("Deactivating capture before quit");
        fSwitchController->OnReturnFromRight(0.5f);
    }
    fSwitchController->Stop();
    fClient->Disconnect();
    RemoveDeskbarReplicant();
    return true;
}

// ------------------------------------------------------------------
// Deskbar replicant
// ------------------------------------------------------------------
void SoftKMClientApp::InstallDeskbarReplicant()
{
    BDeskbar deskbar;
    if (deskbar.HasItem(REPLICANT_NAME))
        deskbar.RemoveItem(REPLICANT_NAME);

    DeskbarReplicant* rep = new DeskbarReplicant(
        BRect(0, 0, 15, 15), REPLICANT_NAME);
    deskbar.AddItem(rep);
    delete rep;
}

void SoftKMClientApp::RemoveDeskbarReplicant()
{
    BDeskbar deskbar;
    if (deskbar.HasItem(REPLICANT_NAME))
        deskbar.RemoveItem(REPLICANT_NAME);
}

// ------------------------------------------------------------------
// Windows
// ------------------------------------------------------------------
void SoftKMClientApp::ShowSettingsWindow()
{
    if (!fSettingsWindow)
        fSettingsWindow = new SettingsWindow();

    if (fSettingsWindow->IsHidden()) fSettingsWindow->Show();
    else                              fSettingsWindow->Activate();
}

void SoftKMClientApp::ShowLogWindow()
{
    if (!fLogWindow) fLogWindow = LogWindow::GetInstance();
    if (fLogWindow->IsHidden()) fLogWindow->Show();
    else                         fLogWindow->Activate();
}

void SoftKMClientApp::ShowAbout()
{
    const char* authors[] = { "Daniel Benjaminsson", nullptr };

    BAboutWindow* about = new BAboutWindow("softKMClient",
        "application/x-vnd.softKMClient");
    about->AddDescription(
        "Software Keyboard/Mouse Switch — Haiku sender\n\n"
        "Captures keyboard and mouse on this (left) Haiku machine and "
        "forwards them to the softKM server running on the right Haiku "
        "machine over a TCP connection.\n\n"
        "Move your mouse to the right screen edge to switch control.");
    about->AddCopyright(2025, "Microgeni AB");
    about->AddAuthors(authors);
    about->Show();
}

#include "SoftKMApp.h"
#include "DeskbarReplicant.h"
#include "SettingsWindow.h"
#include "LogWindow.h"
#include "ClipboardManager.h"
#include "Settings.h"
#include "Logger.h"

// Server-mode includes
#include "NetworkServer.h"
#include "InputInjector.h"

// Client-mode includes
#include "NetworkClient.h"
#include "SwitchController.h"

#include <Deskbar.h>
#include <Roster.h>
#include <Alert.h>
#include <AppFileInfo.h>
#include <File.h>
#include <Messenger.h>
#include <private/interface/AboutWindow.h>

#include <cstdio>
#include <cstring>


SoftKMApp* SoftKMApp::sInstance = nullptr;


// ---------------------------------------------------------------------
// Connection callback (client mode)
//
// NetworkClient calls this from its receive thread when the TCP state
// changes. We forward the change to the application's message loop so
// updates run on the main thread.
// ---------------------------------------------------------------------
static void ClientConnectionChanged(bool connected, void* /*cookie*/)
{
    BMessenger(be_app).SendMessage(connected
        ? MSG_PEER_CONNECTED : MSG_PEER_DISCONNECTED);
}


// =====================================================================
// Construction / destruction
// =====================================================================
SoftKMApp::SoftKMApp()
    : BApplication("application/x-vnd.softKM"),
      fNetworkServer(nullptr),
      fInputInjector(nullptr),
      fNetworkClient(nullptr),
      fSwitchController(nullptr),
      fClipboardManager(nullptr),
      fSettingsWindow(nullptr),
      fLogWindow(nullptr),
      fConnected(false)
{
    sInstance = this;

    // Settings come first — we need GetMode() to know which sub-tree
    // of objects to instantiate.
    Settings::Load();

    // Common: log window + on-disk log file. Both modes use this.
    fLogWindow = LogWindow::GetInstance();
    Logger::Instance().SetLogWindow(BMessenger(fLogWindow));

    // Path of the on-disk log depends on mode so the two roles' logs
    // don't collide if a user briefly tries each one out on the same
    // machine.
    const char* logPath = (Settings::GetMode() == MODE_CLIENT)
        ? "/boot/home/softKM_client.log"
        : "/boot/home/softKM_server.log";
    Logger::Instance().Open(logPath);

    LOG("=== softKM starting in %s mode ===",
        Settings::GetMode() == MODE_CLIENT ? "CLIENT" : "SERVER");

    // Common: clipboard works for both modes (server reads/writes for
    // sync; client reads/writes the same way).
    fClipboardManager = new ClipboardManager();

    // Mode-specific construction
    if (Settings::GetMode() == MODE_CLIENT)
        ConstructClient();
    else
        ConstructServer();
}


void SoftKMApp::ConstructClient()
{
    fNetworkClient    = new NetworkClient();
    fSwitchController = new SwitchController();

    fNetworkClient->SetClipboardManager(fClipboardManager);
    fNetworkClient->SetConnectionCallback(ClientConnectionChanged, nullptr);

    fSwitchController->SetNetworkClient(fNetworkClient);
    fSwitchController->SetClipboardManager(fClipboardManager);
    fSwitchController->SetSwitchEdge(Settings::GetSwitchEdge());
    fSwitchController->SetReturnEdge(Settings::GetReturnEdge());
    fSwitchController->SetDwellTime(Settings::GetDwellTime());
}


void SoftKMApp::ConstructServer()
{
    fInputInjector = new InputInjector();
    fNetworkServer = new NetworkServer(Settings::GetPort(), fInputInjector);

    // Two-way wiring: injector needs the server to send back
    // CONTROL_SWITCH events on return-edge dwell; server needs the
    // injector for reverse-direction edge handling, and the clipboard
    // manager for sync passthrough.
    fInputInjector->SetNetworkServer(fNetworkServer);
    fNetworkServer->SetClipboardManager(fClipboardManager);
}


SoftKMApp::~SoftKMApp()
{
    RemoveDeskbarReplicant();

    // Order matters: stop network first so no incoming events fire
    // during destructor races; then stop the input subsystem.
    if (fNetworkServer)    { fNetworkServer->Stop();    delete fNetworkServer; }
    if (fNetworkClient)    { fNetworkClient->Disconnect(); delete fNetworkClient; }
    if (fSwitchController) { fSwitchController->Stop(); delete fSwitchController; }

    delete fInputInjector;
    delete fClipboardManager;

    Settings::Save();
    sInstance = nullptr;
}


// =====================================================================
// Startup
// =====================================================================
void SoftKMApp::ReadyToRun()
{
    if (Settings::GetMode() == MODE_CLIENT)
        StartClient();
    else
        StartServer();

    InstallDeskbarReplicant();
}


void SoftKMApp::StartClient()
{
    status_t err = fSwitchController->Start();
    if (err != B_OK)
        LOG("SwitchController::Start failed: %s", strerror(err));

    // Don't block ReadyToRun on the connection — it might fail or take
    // a while; NetworkClient handles reconnect internally.
    const char* host = Settings::GetHostAddress();
    uint16 port      = Settings::GetPort();
    LOG("Connecting to %s:%u", host, port);
    fNetworkClient->Connect(host, port);
}


void SoftKMApp::StartServer()
{
    status_t result = fNetworkServer->Start();
    if (result != B_OK) {
        BAlert* alert = new BAlert("softKM",
            "Failed to start network server. Check if the port is "
            "available (another softKM instance may already be running).",
            "OK", nullptr, nullptr,
            B_WIDTH_AS_USUAL, B_STOP_ALERT);
        alert->Go();
    }
}


// =====================================================================
// Status
// =====================================================================
bool SoftKMApp::IsCapturing() const
{
    return fSwitchController != nullptr && fSwitchController->IsCapturing();
}


void SoftKMApp::SetConnected(bool connected)
{
    fConnected = connected;
    LOG("Connection state: %s", connected ? "CONNECTED" : "DISCONNECTED");
}


// =====================================================================
// MessageReceived
// =====================================================================
void SoftKMApp::MessageReceived(BMessage* message)
{
    switch (message->what) {
        // ---------------- Common UI commands ----------------
        case MSG_SHOW_SETTINGS:
            ShowSettingsWindow();
            break;

        case MSG_SHOW_LOG:
            ShowLogWindow();
            break;

        case MSG_TOGGLE_LOG:
            if (fLogWindow != nullptr) {
                if (fLogWindow->IsHidden()) fLogWindow->Show();
                else                         fLogWindow->Hide();
            }
            break;

        case MSG_SHOW_ABOUT:
            ShowAbout();
            break;

        case MSG_QUERY_LOG_VISIBLE:
        {
            BMessage reply(B_REPLY);
            reply.AddBool("visible",
                fLogWindow != nullptr && !fLogWindow->IsHidden());
            message->SendReply(&reply);
            break;
        }

        case MSG_QUERY_STATUS:
        // Accept the legacy 4-cc 'qcst' too, so an older Deskbar
        // replicant that's still cached in the Deskbar process from
        // a previous deploy keeps getting answered. Without this,
        // a stale replicant stays stuck on "Disconnected" forever
        // because its query hits the default case and is dropped.
        case 'qcst':
        {
            BMessage reply(B_REPLY);
            reply.AddBool("connected", fConnected);
            reply.AddBool("capturing", IsCapturing());
            message->SendReply(&reply);
            break;
        }

        case MSG_INSTALL_REPLICANT:
            InstallDeskbarReplicant();
            break;

        case MSG_QUIT_REQUESTED:
            PostMessage(B_QUIT_REQUESTED);
            break;

        // ---------------- Server-mode events ----------------
        case MSG_CLIENT_CONNECTED:
            SetConnected(true);
            break;

        case MSG_CLIENT_DISCONNECTED:
            SetConnected(false);
            break;

        case MSG_INPUT_EVENT:
            if (fInputInjector != nullptr)
                fInputInjector->ProcessEvent(message);
            break;

        // ---------------- Client-mode events ----------------
        case MSG_PEER_CONNECTED:
            SetConnected(true);
            // Push our switch-edge config to the server so it knows
            // which edge to use for return-trip detection.
            if (fNetworkClient != nullptr) {
                fNetworkClient->SendSettingsSync(
                    Settings::GetDwellTime(),
                    Settings::GetSwitchEdge(),
                    Settings::GetReturnEdge(),
                    0.0f);
            }
            break;

        case MSG_PEER_DISCONNECTED:
            SetConnected(false);
            // If we were mid-capture when the connection dropped, force
            // local deactivation so the cursor isn't stuck locked.
            if (fSwitchController != nullptr
                && fSwitchController->IsCapturing()) {
                fSwitchController->OnReturnFromRight(0.5f);
            }
            break;

        case MSG_SWITCH_TO_LEFT:
        {
            float yRatio = 0.5f;
            message->FindFloat("yRatio", &yRatio);
            if (fSwitchController != nullptr)
                fSwitchController->OnReturnFromRight(yRatio);
            break;
        }

        case MSG_CAPTURE_ACTIVATED:
        case MSG_CAPTURE_DEACTIVATED:
            // No-op for now; the Deskbar replicant polls for state.
            break;

        case MSG_RECONNECT:
            // Settings changed — apply to live components and reconnect.
            // Only meaningful in client mode.
            if (fSwitchController != nullptr && fNetworkClient != nullptr) {
                fSwitchController->SetSwitchEdge(Settings::GetSwitchEdge());
                fSwitchController->SetReturnEdge(Settings::GetReturnEdge());
                fSwitchController->SetDwellTime(Settings::GetDwellTime());
                fNetworkClient->Disconnect();
                fNetworkClient->Connect(
                    Settings::GetHostAddress(), Settings::GetPort());
            }
            break;

        default:
            BApplication::MessageReceived(message);
            break;
    }
}


// =====================================================================
// QuitRequested
// =====================================================================
bool SoftKMApp::QuitRequested()
{
    // Server: if we're injecting events for a connected client, return
    // control to the sender first so its cursor isn't left frozen.
    if (fInputInjector != nullptr && fInputInjector->IsActive()) {
        LOG("Returning control to peer before quitting...");
        if (fNetworkServer != nullptr)
            fNetworkServer->SendControlSwitch(1, 0.5f);
        fInputInjector->SetActive(false);
    }

    // Client: if we're currently capturing, deactivate cleanly.
    if (fSwitchController != nullptr && fSwitchController->IsCapturing()) {
        LOG("Deactivating capture before quit");
        fSwitchController->OnReturnFromRight(0.5f);
    }

    if (fNetworkServer)    fNetworkServer->Stop();
    if (fSwitchController) fSwitchController->Stop();
    if (fNetworkClient)    fNetworkClient->Disconnect();

    RemoveDeskbarReplicant();
    return true;
}


// =====================================================================
// Deskbar replicant
// =====================================================================
void SoftKMApp::InstallDeskbarReplicant()
{
    BDeskbar deskbar;

    if (deskbar.HasItem(REPLICANT_NAME))
        deskbar.RemoveItem(REPLICANT_NAME);

    DeskbarReplicant* replicant = new DeskbarReplicant(
        BRect(0, 0, 15, 15), REPLICANT_NAME);

    status_t result = deskbar.AddItem(replicant);
    delete replicant;

    if (result != B_OK)
        fprintf(stderr, "Failed to install Deskbar replicant: %s\n",
            strerror(result));
}


void SoftKMApp::RemoveDeskbarReplicant()
{
    BDeskbar deskbar;
    if (deskbar.HasItem(REPLICANT_NAME))
        deskbar.RemoveItem(REPLICANT_NAME);
}


// =====================================================================
// Windows
// =====================================================================
void SoftKMApp::ShowSettingsWindow()
{
    if (fSettingsWindow == nullptr)
        fSettingsWindow = new SettingsWindow();

    if (fSettingsWindow->IsHidden())
        fSettingsWindow->Show();
    else
        fSettingsWindow->Activate();
}


void SoftKMApp::ShowLogWindow()
{
    if (fLogWindow == nullptr)
        fLogWindow = LogWindow::GetInstance();

    if (fLogWindow->IsHidden())
        fLogWindow->Show();
    else
        fLogWindow->Activate();
}


void SoftKMApp::ShowAbout()
{
    app_info appInfo;
    GetAppInfo(&appInfo);
    BFile file(&appInfo.ref, B_READ_ONLY);
    BAppFileInfo appFileInfo(&file);

    version_info versionInfo;
    char versionString[256] = "";
    if (appFileInfo.GetVersionInfo(&versionInfo, B_APP_VERSION_KIND) == B_OK) {
        // Dev builds have default version 1.0.0, just show build number
        if (versionInfo.major == 1 && versionInfo.middle == 0 && versionInfo.minor == 0) {
            snprintf(versionString, sizeof(versionString), "Build dev %lu",
                (unsigned long)versionInfo.internal);
        } else {
            snprintf(versionString, sizeof(versionString), "Version %lu.%lu.%lu (%lu)",
                (unsigned long)versionInfo.major,
                (unsigned long)versionInfo.middle,
                (unsigned long)versionInfo.minor,
                (unsigned long)versionInfo.internal);
        }
    }

    const char* authors[] = {
        "Daniel Benjaminsson",
        nullptr
    };

    BAboutWindow* about = new BAboutWindow("softKM",
        "application/x-vnd.softKM");
    about->SetVersion(versionString);
    about->AddDescription(
        "Software Keyboard/Mouse Switch for Haiku\n\n"
        "Share keyboard and mouse input between two computers over a "
        "network. One side runs in Client mode (captures input and "
        "forwards it); the other side runs in Server mode (receives "
        "and injects).\n\n"
        "Move your mouse to the configured screen edge to seamlessly "
        "transfer control between machines.");
    about->AddCopyright(2025, "Microgeni AB");
    about->AddAuthors(authors);
    about->Show();
}

#ifndef SOFTKM_APP_H
#define SOFTKM_APP_H

#include <Application.h>
#include <Messenger.h>

class NetworkServer;        // server mode
class InputInjector;        // server mode
class NetworkClient;        // client mode
class SwitchController;     // client mode
class ClipboardManager;     // both
class SettingsWindow;       // both
class LogWindow;            // both

// Application messages.
//
// 'sset', 'slog', etc. are inherited from the original HaikuOS server
// app. Client-mode-specific messages were originally in
// HaikuClient/src/SoftKMClientApp.h with different four-char codes
// ('nCon', 'nDis', 'cACT', 'cDEA', 'StoL'); we merge them in here so
// one BApplication can handle either role.
enum {
    // Common (sent by Deskbar / Settings / Log windows)
    MSG_SHOW_SETTINGS           = 'sset',
    MSG_SHOW_LOG                = 'slog',
    MSG_TOGGLE_LOG              = 'tlog',
    MSG_QUERY_LOG_VISIBLE       = 'qlog',
    MSG_SHOW_ABOUT              = 'sabt',
    MSG_QUERY_STATUS            = 'qsts',  // unified status query
    MSG_INSTALL_REPLICANT       = 'irep',
    MSG_QUIT_REQUESTED          = 'quit',

    // Server-mode (set by NetworkServer thread / InputInjector)
    MSG_CLIENT_CONNECTED        = 'ccon',
    MSG_CLIENT_DISCONNECTED     = 'cdis',
    MSG_INPUT_EVENT             = 'inev',

    // Client-mode (set by NetworkClient callback / SwitchController)
    MSG_PEER_CONNECTED          = 'nCon',
    MSG_PEER_DISCONNECTED       = 'nDis',
    MSG_CAPTURE_ACTIVATED       = 'cACT',
    MSG_CAPTURE_DEACTIVATED     = 'cDEA',
    MSG_SWITCH_TO_LEFT          = 'StoL',  // server says 'return to sender'
    MSG_RECONNECT               = 'rCON',  // settings changed, redo connection

    // Legacy aliases kept for compatibility with existing Deskbar
    // replicant / SettingsWindow code that used the older names.
    MSG_QUERY_CONNECTION_STATUS = MSG_QUERY_STATUS,
    MSG_CONNECTION_STATUS       = 'csts'
};

class SoftKMApp : public BApplication {
public:
    SoftKMApp();
    virtual ~SoftKMApp();

    virtual void ReadyToRun() override;
    virtual void MessageReceived(BMessage* message) override;
    virtual bool QuitRequested() override;

    // Status accessors used by the Deskbar replicant.
    bool IsConnected() const { return fConnected; }
    bool IsCapturing() const;   // always false in server mode

    static SoftKMApp* GetInstance() { return sInstance; }

private:
    // Mode-specific construction. Constructor calls one of these.
    void ConstructClient();
    void ConstructServer();

    // Mode-specific startup. ReadyToRun() calls one of these.
    void StartClient();
    void StartServer();

    void InstallDeskbarReplicant();
    void RemoveDeskbarReplicant();
    void ShowSettingsWindow();
    void ShowLogWindow();
    void ShowAbout();

    void SetConnected(bool connected);

    // Server-mode objects (nullptr in client mode)
    NetworkServer*    fNetworkServer;
    InputInjector*    fInputInjector;

    // Client-mode objects (nullptr in server mode)
    NetworkClient*    fNetworkClient;
    SwitchController* fSwitchController;

    // Common
    ClipboardManager* fClipboardManager;
    SettingsWindow*   fSettingsWindow;
    LogWindow*        fLogWindow;

    bool fConnected;

    static SoftKMApp* sInstance;
};

#endif // SOFTKM_APP_H

#ifndef SOFTKM_CLIENT_APP_H
#define SOFTKM_CLIENT_APP_H

#include <Application.h>

// Messages from/to sub-components
enum {
    // Network
    MSG_PEER_CONNECTED      = 'nCon',
    MSG_PEER_DISCONNECTED   = 'nDis',

    // Switch controller → app
    MSG_CAPTURE_ACTIVATED   = 'cACT',
    MSG_CAPTURE_DEACTIVATED = 'cDEA',
    MSG_SWITCH_TO_LEFT      = 'StoL',  // right Haiku says "return to sender"

    // UI commands (from Deskbar menu / SettingsWindow)
    MSG_SHOW_SETTINGS       = 'wSET',
    MSG_SHOW_LOG            = 'wLOG',
    MSG_TOGGLE_LOG          = 'wTLG',
    MSG_SHOW_ABOUT          = 'wABT',
    MSG_QUIT_REQUESTED      = 'wQIT',

    // Polling
    MSG_POLL_STATUS         = 'pSTA',
    MSG_QUERY_STATUS        = 'qSTA',
    MSG_CONNECTION_STATUS   = 'xSTA',

    // Internal
    MSG_INSTALL_REPLICANT   = 'iREP',
};

#define REPLICANT_NAME "softKMClient"

class NetworkClient;
class SwitchController;
class ClipboardManager;
class SettingsWindow;
class LogWindow;

class SoftKMClientApp : public BApplication {
public:
    SoftKMClientApp();
    virtual ~SoftKMClientApp();

    virtual void ReadyToRun();
    virtual void MessageReceived(BMessage* message);
    virtual bool QuitRequested();

    static SoftKMClientApp* Instance() { return sInstance; }

    bool IsConnected() const  { return fConnected; }
    bool IsCapturing() const;

private:
    void SetConnected(bool v);

    void InstallDeskbarReplicant();
    void RemoveDeskbarReplicant();

    void ShowSettingsWindow();
    void ShowLogWindow();
    void ShowAbout();

    // Called when right Haiku returns control
    void OnReturnFromRight(float yRatio);

    static SoftKMClientApp* sInstance;

    NetworkClient*    fClient;
    SwitchController* fSwitchController;
    ClipboardManager* fClipboardManager;
    SettingsWindow*   fSettingsWindow;
    LogWindow*        fLogWindow;

    bool fConnected;
};

#endif // SOFTKM_CLIENT_APP_H

#ifndef SOFTKM_APP_H
#define SOFTKM_APP_H

#include <Application.h>
#include <Messenger.h>

class NetworkServer;
class InputInjector;
class ClipboardManager;
class SettingsWindow;
class LogWindow;

// Application messages
enum {
    MSG_SHOW_SETTINGS = 'sset',
    MSG_SHOW_LOG = 'slog',
    MSG_TOGGLE_LOG = 'tlog',
    MSG_QUERY_LOG_VISIBLE = 'qlog',
    MSG_CONNECTION_STATUS = 'csts',
    MSG_QUERY_CONNECTION_STATUS = 'qcst',
    MSG_CLIENT_CONNECTED = 'ccon',
    MSG_CLIENT_DISCONNECTED = 'cdis',
    MSG_INPUT_EVENT = 'inev',
    MSG_INSTALL_REPLICANT = 'irep',
    MSG_QUIT_REQUESTED = 'quit'
};

class SoftKMApp : public BApplication {
public:
    SoftKMApp();
    virtual ~SoftKMApp();

    virtual void ReadyToRun() override;
    virtual void MessageReceived(BMessage* message) override;
    virtual bool QuitRequested() override;

    bool IsClientConnected() const { return fClientConnected; }
    void SetClientConnected(bool connected);

    static SoftKMApp* GetInstance() { return sInstance; }

private:
    void InstallDeskbarReplicant();
    void RemoveDeskbarReplicant();
    void ShowSettingsWindow();
    void ShowLogWindow();

    NetworkServer* fNetworkServer;
    InputInjector* fInputInjector;
    ClipboardManager* fClipboardManager;
    SettingsWindow* fSettingsWindow;
    LogWindow* fLogWindow;
    bool fClientConnected;

    static SoftKMApp* sInstance;
};

#endif // SOFTKM_APP_H

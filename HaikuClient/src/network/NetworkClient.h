#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#include <OS.h>
#include <SupportDefs.h>
#include <String.h>

class ClipboardManager;

// ------------------------------------------------------------------
// NetworkClient
//
// Connects (as a TCP client) to the softKM server running on the
// right-hand Haiku machine and sends encoded input events.
// Mirror image of the macOS NetworkClient.swift.
// ------------------------------------------------------------------
class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    // Connect / disconnect
    status_t Connect(const char* host, uint16 port);
    void     Disconnect();
    bool     IsConnected() const { return fSocket >= 0; }

    // Send raw protocol bytes
    bool Send(const uint8* data, size_t length);

    // High-level event senders (encode + send)
    void SendKeyDown(uint32 haikuKey, uint32 modifiers,
                     const char* bytes, uint8 numBytes);
    void SendKeyUp(uint32 haikuKey, uint32 modifiers);

    void SendMouseMove(float dx, float dy, bool relative, uint32 modifiers);
    void SendMouseDown(uint32 buttons, float x, float y,
                       uint32 modifiers, uint32 clicks);
    void SendMouseUp(uint32 buttons, float x, float y, uint32 modifiers);
    void SendMouseWheel(float deltaX, float deltaY, uint32 modifiers);

    void SendControlSwitch(uint8 direction, float yRatio);
    void SendScreenInfo(float width, float height);
    void SendSettingsSync(float dwellTime, uint8 leftEdge, uint8 rightReturnEdge,
                          float yOffsetRatio);
    void SendHeartbeat();
    void SendClipboard(const uint8* data, uint32 length);

    // Batched mouse-move flush (called by a timer)
    void FlushPendingMouseMove();

    // Callback when connection is lost / gained (set by SwitchController)
    void SetConnectionCallback(void (*cb)(bool connected, void* cookie),
                               void* cookie);

    // Remote screen size (received from right Haiku server)
    float RemoteWidth()  const { return fRemoteWidth; }
    float RemoteHeight() const { return fRemoteHeight; }

    void SetClipboardManager(ClipboardManager* mgr) { fClipboardManager = mgr; }

private:
    // Encoding helpers
    static void AppendUInt16(uint8* buf, size_t& off, uint16 v);
    static void AppendUInt32(uint8* buf, size_t& off, uint32 v);
    static void AppendFloat (uint8* buf, size_t& off, float  v);
    static size_t BuildHeader(uint8* buf, uint8 eventType, uint32 payloadLen);

    // Background receive thread
    static int32 ReceiveThreadFunc(void* data);
    void         ReceiveLoop();
    void         HandleReceivedMessage(const uint8* data, size_t length);

    // Reconnect loop
    static int32 ReconnectThreadFunc(void* data);
    void         ReconnectLoop();
    void         ScheduleReconnect();

    int          fSocket;
    BString      fHost;
    uint16       fPort;

    thread_id    fReceiveThread;
    thread_id    fReconnectThread;
    volatile bool fRunning;
    volatile bool fWantReconnect;

    // Pending batched mouse delta
    float        fPendingDX;
    float        fPendingDY;
    uint32       fPendingMouseMods;
    bool         fHasPendingMouse;
    sem_id       fBatchLock;

    // Heartbeat
    thread_id    fHeartbeatThread;
    static int32 HeartbeatThreadFunc(void* data);
    void         HeartbeatLoop();

    float        fRemoteWidth;
    float        fRemoteHeight;

    void         (*fConnectionCb)(bool connected, void* cookie);
    void*        fConnectionCbCookie;

    ClipboardManager* fClipboardManager;
};

#endif // NETWORK_CLIENT_H

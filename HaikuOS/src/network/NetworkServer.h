#ifndef NETWORK_SERVER_H
#define NETWORK_SERVER_H

#include <OS.h>
#include <SupportDefs.h>

class InputInjector;

class NetworkServer {
public:
    NetworkServer(uint16 port, InputInjector* injector);
    ~NetworkServer();

    status_t Start();
    void Stop();

    bool IsRunning() const { return fRunning; }
    bool HasClient() const { return fClientSocket >= 0; }

    void SendControlSwitch(uint8 direction, float yFromBottom = 0.0f);  // 0=toHaiku, 1=toMac
    void SendScreenInfo();

    // Screen dimensions
    float GetLocalWidth() const { return fLocalWidth; }
    float GetLocalHeight() const { return fLocalHeight; }
    float GetRemoteWidth() const { return fRemoteWidth; }
    float GetRemoteHeight() const { return fRemoteHeight; }

private:
    static int32 ListenThreadFunc(void* data);
    static int32 ClientThreadFunc(void* data);

    void AcceptConnections();
    void HandleClient(int clientSocket);
    void ProcessMessage(const uint8* data, size_t length);
    void SendHeartbeatAck();

    uint16 fPort;
    InputInjector* fInputInjector;
    int fServerSocket;
    int fClientSocket;
    thread_id fListenThread;
    thread_id fClientThread;
    volatile bool fRunning;

    // Screen dimensions
    float fLocalWidth;
    float fLocalHeight;
    float fRemoteWidth;
    float fRemoteHeight;
};

#endif // NETWORK_SERVER_H

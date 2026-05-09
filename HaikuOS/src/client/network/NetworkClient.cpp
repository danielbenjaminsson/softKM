#include "NetworkClient.h"
#include "Protocol.h"
#include "ClipboardManager.h"
#include "Logger.h"

#include <Messenger.h>
#include <Application.h>
#include <Screen.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <cstdlib>

// Message codes posted to be_app via the connection callback
// (MSG_PEER_CONNECTED, MSG_PEER_DISCONNECTED) are defined in
// SoftKMApp.h. This file doesn't reference them by name, only
// through the callback function pointer set via
// SetConnectionCallback().

// ------------------------------------------------------------------
// Construction / destruction
// ------------------------------------------------------------------
NetworkClient::NetworkClient()
    : fSocket(-1),
      fPort(31337),
      fReceiveThread(-1),
      fReconnectThread(-1),
      fRunning(false),
      fWantReconnect(false),
      fPendingDX(0), fPendingDY(0),
      fPendingMouseMods(0),
      fHasPendingMouse(false),
      fHeartbeatThread(-1),
      fRemoteWidth(0), fRemoteHeight(0),
      fConnectionCb(nullptr),
      fConnectionCbCookie(nullptr),
      fClipboardManager(nullptr)
{
    fBatchLock = create_sem(1, "softkm_batch_lock");
}

NetworkClient::~NetworkClient()
{
    Disconnect();
    delete_sem(fBatchLock);
}

// ------------------------------------------------------------------
// Encoding helpers
// ------------------------------------------------------------------
void NetworkClient::AppendUInt16(uint8* buf, size_t& off, uint16 v)
{
    uint16 le = v;  // already little-endian on x86
    memcpy(buf + off, &le, 2);
    off += 2;
}

void NetworkClient::AppendUInt32(uint8* buf, size_t& off, uint32 v)
{
    uint32 le = v;
    memcpy(buf + off, &le, 4);
    off += 4;
}

void NetworkClient::AppendFloat(uint8* buf, size_t& off, float v)
{
    uint32 bits;
    memcpy(&bits, &v, 4);
    AppendUInt32(buf, off, bits);
}

size_t NetworkClient::BuildHeader(uint8* buf, uint8 eventType, uint32 payloadLen)
{
    size_t off = 0;
    AppendUInt16(buf, off, PROTOCOL_MAGIC);
    buf[off++] = PROTOCOL_VERSION;
    buf[off++] = eventType;
    AppendUInt32(buf, off, payloadLen);
    return off;  // = sizeof(ProtocolHeader) = 8
}

// ------------------------------------------------------------------
// Connect / Disconnect
// ------------------------------------------------------------------
status_t NetworkClient::Connect(const char* host, uint16 port)
{
    Disconnect();

    fHost = host;
    fPort = port;
    fRunning = true;
    fWantReconnect = false;

    // Resolve
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", port);

    struct addrinfo* res = nullptr;
    int err = getaddrinfo(host, portStr, &hints, &res);
    if (err != 0) {
        LOG("NetworkClient: DNS failed for %s: %s", host, gai_strerror(err));
        fRunning = false;
        ScheduleReconnect();
        return B_ERROR;
    }

    fSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (fSocket < 0) {
        LOG("NetworkClient: socket() failed: %s", strerror(errno));
        freeaddrinfo(res);
        fRunning = false;
        ScheduleReconnect();
        return B_ERROR;
    }

    // TCP_NODELAY for low latency
    int opt = 1;
    setsockopt(fSocket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    if (connect(fSocket, res->ai_addr, res->ai_addrlen) < 0) {
        LOG("NetworkClient: connect() failed: %s", strerror(errno));
        close(fSocket);
        fSocket = -1;
        freeaddrinfo(res);
        ScheduleReconnect();
        return B_ERROR;
    }

    freeaddrinfo(res);
    LOG("NetworkClient: connected to %s:%u", host, port);

    // Notify app via the registered callback; the callback in
    // SoftKMClientApp will post MSG_PEER_CONNECTED to be_app. We
    // intentionally do NOT send MSG_PEER_CONNECTED directly here as
    // well — that would deliver the message twice and trigger two
    // SendSettingsSync calls.
    if (fConnectionCb) fConnectionCb(true, fConnectionCbCookie);

    // Send our screen size to the right Haiku
    BScreen screen;
    BRect frame = screen.Frame();
    SendScreenInfo(frame.Width() + 1, frame.Height() + 1);

    // Start receive thread
    fReceiveThread = spawn_thread(ReceiveThreadFunc, "softkm_client_recv",
                                  B_NORMAL_PRIORITY, this);
    if (fReceiveThread >= 0)
        resume_thread(fReceiveThread);

    // Start heartbeat thread
    fHeartbeatThread = spawn_thread(HeartbeatThreadFunc, "softkm_heartbeat",
                                    B_LOW_PRIORITY, this);
    if (fHeartbeatThread >= 0)
        resume_thread(fHeartbeatThread);

    return B_OK;
}

void NetworkClient::Disconnect()
{
    fRunning = false;

    if (fSocket >= 0) {
        shutdown(fSocket, SHUT_RDWR);
        close(fSocket);
        fSocket = -1;
    }

    if (fReceiveThread >= 0) {
        status_t result;
        wait_for_thread(fReceiveThread, &result);
        fReceiveThread = -1;
    }

    if (fHeartbeatThread >= 0) {
        status_t result;
        wait_for_thread(fHeartbeatThread, &result);
        fHeartbeatThread = -1;
    }
}

// ------------------------------------------------------------------
// Send raw bytes
// ------------------------------------------------------------------
bool NetworkClient::Send(const uint8* data, size_t length)
{
    if (fSocket < 0) {
        LOG("NetworkClient: Send() called with fSocket=-1 (len=%zu)", length);
        return false;
    }

    // MSG_NOSIGNAL prevents SIGPIPE if the peer has closed — without it,
    // a broken pipe during send() would kill our process silently.
    ssize_t sent = send(fSocket, data, length, MSG_NOSIGNAL);
    if (sent < 0) {
        LOG("NetworkClient: send() failed (len=%zu): %s",
            length, strerror(errno));

        // Tear the connection down so IsConnected() reflects reality
        // and ScheduleReconnect kicks in. ReceiveLoop's blocking recv()
        // can hold a stale fd open for a long time even after the peer
        // has gone, but a send failure is an immediate, reliable signal
        // that the connection is dead.
        int sock = fSocket;
        fSocket = -1;
        if (sock >= 0) {
            shutdown(sock, SHUT_RDWR);  // unblocks recv() in ReceiveLoop
            close(sock);
        }
        if (fConnectionCb) fConnectionCb(false, fConnectionCbCookie);
        ScheduleReconnect();
        return false;
    }
    if ((size_t)sent != length) {
        LOG("NetworkClient: short send (sent=%zd of %zu)", sent, length);
    }
    return true;
}

// ------------------------------------------------------------------
// Event senders
// ------------------------------------------------------------------
void NetworkClient::SendKeyDown(uint32 haikuKey, uint32 modifiers,
                                const char* bytes, uint8 numBytes)
{
    // Header(8) + keyCode(4) + modifiers(4) + numBytes(1) + bytes
    size_t payloadLen = 4 + 4 + 1 + numBytes;
    uint8 buf[8 + 4 + 4 + 1 + 16];   // max 16 UTF-8 bytes
    size_t off = BuildHeader(buf, EVENT_KEY_DOWN, (uint32)payloadLen);
    AppendUInt32(buf, off, haikuKey);
    AppendUInt32(buf, off, modifiers);
    buf[off++] = numBytes;
    if (numBytes > 0 && bytes != nullptr) {
        memcpy(buf + off, bytes, numBytes);
        off += numBytes;
    }
    Send(buf, off);
}

void NetworkClient::SendKeyUp(uint32 haikuKey, uint32 modifiers)
{
    uint8 buf[8 + 8];
    size_t off = BuildHeader(buf, EVENT_KEY_UP, 8);
    AppendUInt32(buf, off, haikuKey);
    AppendUInt32(buf, off, modifiers);
    Send(buf, off);
}

void NetworkClient::SendMouseMove(float dx, float dy, bool relative,
                                  uint32 modifiers)
{
    uint8 buf[8 + sizeof(MouseMovePayload)];
    size_t off = BuildHeader(buf, EVENT_MOUSE_MOVE,
                             (uint32)sizeof(MouseMovePayload));
    AppendFloat(buf, off, dx);
    AppendFloat(buf, off, dy);
    buf[off++] = relative ? 1 : 0;
    AppendUInt32(buf, off, modifiers);
    Send(buf, off);
}

void NetworkClient::FlushPendingMouseMove()
{
    acquire_sem(fBatchLock);
    if (!fHasPendingMouse) {
        release_sem(fBatchLock);
        return;
    }
    float dx = fPendingDX;
    float dy = fPendingDY;
    uint32 mods = fPendingMouseMods;
    fPendingDX = fPendingDY = 0;
    fHasPendingMouse = false;
    release_sem(fBatchLock);

    SendMouseMove(dx, dy, true, mods);
}

void NetworkClient::SendMouseDown(uint32 buttons, float x, float y,
                                  uint32 modifiers, uint32 clicks)
{
    uint8 buf[8 + sizeof(MouseDownPayload)];
    size_t off = BuildHeader(buf, EVENT_MOUSE_DOWN,
                             (uint32)sizeof(MouseDownPayload));
    AppendUInt32(buf, off, buttons);
    AppendFloat (buf, off, x);
    AppendFloat (buf, off, y);
    AppendUInt32(buf, off, modifiers);
    AppendUInt32(buf, off, clicks);
    Send(buf, off);
}

void NetworkClient::SendMouseUp(uint32 buttons, float x, float y,
                                uint32 modifiers)
{
    uint8 buf[8 + sizeof(MouseButtonPayload)];
    size_t off = BuildHeader(buf, EVENT_MOUSE_UP,
                             (uint32)sizeof(MouseButtonPayload));
    AppendUInt32(buf, off, buttons);
    AppendFloat (buf, off, x);
    AppendFloat (buf, off, y);
    AppendUInt32(buf, off, modifiers);
    Send(buf, off);
}

void NetworkClient::SendMouseWheel(float deltaX, float deltaY, uint32 modifiers)
{
    uint8 buf[8 + sizeof(MouseWheelPayload)];
    size_t off = BuildHeader(buf, EVENT_MOUSE_WHEEL,
                             (uint32)sizeof(MouseWheelPayload));
    AppendFloat (buf, off, deltaX);
    AppendFloat (buf, off, deltaY);
    AppendUInt32(buf, off, modifiers);
    Send(buf, off);
}

void NetworkClient::SendControlSwitch(uint8 direction, float yRatio)
{
    uint8 buf[8 + sizeof(ControlSwitchPayload)];
    size_t off = BuildHeader(buf, EVENT_CONTROL_SWITCH,
                             (uint32)sizeof(ControlSwitchPayload));
    buf[off++] = direction;
    AppendFloat(buf, off, yRatio);
    Send(buf, off);
}

void NetworkClient::SendScreenInfo(float width, float height)
{
    uint8 buf[8 + sizeof(ScreenInfoPayload)];
    size_t off = BuildHeader(buf, EVENT_SCREEN_INFO,
                             (uint32)sizeof(ScreenInfoPayload));
    AppendFloat(buf, off, width);
    AppendFloat(buf, off, height);
    // Tell the receiver we're a Haiku sender. On the receiving side
    // this disables the macOS->Haiku keycode translation table, since
    // we already produce native Haiku keycodes.
    buf[off++] = SENDER_HAIKU;
    Send(buf, off);
    LOG("NetworkClient: sent screen info %.0fx%.0f (sender=Haiku)", width, height);
}

void NetworkClient::SendSettingsSync(float dwellTime, uint8 leftEdge,
                                     uint8 rightReturnEdge, float yOffsetRatio)
{
    uint8 buf[8 + sizeof(SettingsSyncPayload)];
    size_t off = BuildHeader(buf, EVENT_SETTINGS_SYNC,
                             (uint32)sizeof(SettingsSyncPayload));
    AppendFloat(buf, off, dwellTime);
    buf[off++] = leftEdge;
    buf[off++] = rightReturnEdge;
    AppendFloat(buf, off, yOffsetRatio);
    Send(buf, off);
}

void NetworkClient::SendHeartbeat()
{
    uint8 buf[8];
    size_t off = BuildHeader(buf, EVENT_HEARTBEAT, 0);
    Send(buf, off);
}

void NetworkClient::SendClipboard(const uint8* data, uint32 length)
{
    if (data == nullptr || length == 0) return;

    size_t totalSize = 8 + sizeof(ClipboardSyncPayload) + length;
    uint8* buf = new uint8[totalSize];

    size_t off = BuildHeader(buf, EVENT_CLIPBOARD_SYNC,
                             (uint32)(sizeof(ClipboardSyncPayload) + length));
    buf[off++] = 0x00;  // plain text
    AppendUInt32(buf, off, length);
    memcpy(buf + off, data, length);
    off += length;

    Send(buf, totalSize);
    delete[] buf;
    LOG("NetworkClient: sent clipboard %lu bytes", length);
}

void NetworkClient::SetConnectionCallback(void (*cb)(bool connected, void* cookie),
                                          void* cookie)
{
    fConnectionCb = cb;
    fConnectionCbCookie = cookie;
}

// ------------------------------------------------------------------
// Receive thread — handles inbound messages from right Haiku
// ------------------------------------------------------------------
int32 NetworkClient::ReceiveThreadFunc(void* data)
{
    ((NetworkClient*)data)->ReceiveLoop();
    return 0;
}

void NetworkClient::ReceiveLoop()
{
    uint8 buf[4096];
    size_t accumulated = 0;
    ssize_t lastN = 0;
    int     lastErrno = 0;

    while (fRunning && fSocket >= 0) {
        ssize_t n = recv(fSocket, buf + accumulated,
                         sizeof(buf) - accumulated, 0);
        if (n <= 0) {
            lastN = n;
            lastErrno = errno;
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        accumulated += n;

        while (accumulated >= sizeof(ProtocolHeader)) {
            ProtocolHeader* hdr = (ProtocolHeader*)buf;
            if (hdr->magic != PROTOCOL_MAGIC) {
                LOG("NetworkClient: bad magic 0x%04X, resetting", hdr->magic);
                accumulated = 0;
                break;
            }
            size_t msgLen = sizeof(ProtocolHeader) + hdr->length;
            if (accumulated < msgLen) break;

            HandleReceivedMessage(buf, msgLen);

            if (accumulated > msgLen)
                memmove(buf, buf + msgLen, accumulated - msgLen);
            accumulated -= msgLen;
        }
    }

    LOG("NetworkClient: receive loop ended (recv=%zd errno=%d %s, accumulated=%zu, fRunning=%d, fSocket=%d)",
        lastN, lastErrno, lastErrno ? strerror(lastErrno) : "ok",
        accumulated, (int)fRunning, fSocket);

    if (fRunning) {
        // Unexpected disconnect
        int sock = fSocket;
        if (sock >= 0) {
            close(sock);
            fSocket = -1;
        }
        // Callback delivers MSG_PEER_DISCONNECTED via app messenger;
        // don't double-post it.
        if (fConnectionCb) fConnectionCb(false, fConnectionCbCookie);
        ScheduleReconnect();
    }
}

void NetworkClient::HandleReceivedMessage(const uint8* data, size_t length)
{
    if (length < sizeof(ProtocolHeader)) return;

    const ProtocolHeader* hdr = (const ProtocolHeader*)data;
    const uint8* payload = data + sizeof(ProtocolHeader);

    switch (hdr->eventType) {
        case EVENT_HEARTBEAT_ACK:
            // Silently acknowledged
            break;

        case EVENT_CONTROL_SWITCH:
        {
            // Right Haiku is returning control back to us
            if (hdr->length >= sizeof(ControlSwitchPayload)) {
                const ControlSwitchPayload* p = (const ControlSwitchPayload*)payload;
                LOG("NetworkClient: CONTROL_SWITCH dir=%d yRatio=%.2f",
                    p->direction, p->yRatio);
                if (p->direction == 1) {
                    // direction=1 means "switch back to sender" — post to app
                    BMessage msg('StoL');   // Switch to Left
                    msg.AddFloat("yRatio", p->yRatio);
                    BMessenger(be_app).SendMessage(&msg);
                }
            }
            break;
        }

        case EVENT_SCREEN_INFO:
        {
            if (hdr->length >= sizeof(ScreenInfoPayload)) {
                const ScreenInfoPayload* p = (const ScreenInfoPayload*)payload;
                fRemoteWidth  = p->width;
                fRemoteHeight = p->height;
                LOG("NetworkClient: remote screen %.0fx%.0f",
                    fRemoteWidth, fRemoteHeight);
            }
            break;
        }

        case EVENT_CLIPBOARD_SYNC:
        {
            if (hdr->length >= sizeof(ClipboardSyncPayload)) {
                const ClipboardSyncPayload* p = (const ClipboardSyncPayload*)payload;
                uint32 dataLen = p->dataLength;
                if (hdr->length >= sizeof(ClipboardSyncPayload) + dataLen &&
                    fClipboardManager != nullptr) {
                    const uint8* clipData = payload + sizeof(ClipboardSyncPayload);
                    fClipboardManager->SetClipboardFromSync(
                        p->contentType, clipData, dataLen);
                }
            }
            break;
        }

        default:
            LOG("NetworkClient: unexpected event type 0x%02X", hdr->eventType);
            break;
    }
}

// ------------------------------------------------------------------
// Heartbeat thread — pings right Haiku every 5 s
// ------------------------------------------------------------------
int32 NetworkClient::HeartbeatThreadFunc(void* data)
{
    ((NetworkClient*)data)->HeartbeatLoop();
    return 0;
}

void NetworkClient::HeartbeatLoop()
{
    while (fRunning) {
        snooze(5000000);  // 5 seconds
        if (!fRunning) break;
        SendHeartbeat();
    }
}

// ------------------------------------------------------------------
// Reconnect
// ------------------------------------------------------------------
int32 NetworkClient::ReconnectThreadFunc(void* data)
{
    ((NetworkClient*)data)->ReconnectLoop();
    return 0;
}

void NetworkClient::ReconnectLoop()
{
    snooze(5000000);  // wait 5 s before retrying
    if (!fWantReconnect) return;
    fWantReconnect = false;
    LOG("NetworkClient: attempting reconnect to %s:%u", fHost.String(), fPort);
    Connect(fHost.String(), fPort);
}

void NetworkClient::ScheduleReconnect()
{
    if (fWantReconnect) return;
    fWantReconnect = true;

    if (fReconnectThread >= 0) {
        // Detach the old thread; it will finish on its own
        fReconnectThread = -1;
    }

    fReconnectThread = spawn_thread(ReconnectThreadFunc, "softkm_reconnect",
                                    B_LOW_PRIORITY, this);
    if (fReconnectThread >= 0)
        resume_thread(fReconnectThread);
}

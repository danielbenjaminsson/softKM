#include "NetworkServer.h"
#include "Protocol.h"
#include "../input/InputInjector.h"
#include "../SoftKMApp.h"

#include <Messenger.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cerrno>

NetworkServer::NetworkServer(uint16 port, InputInjector* injector)
    : fPort(port),
      fInputInjector(injector),
      fServerSocket(-1),
      fClientSocket(-1),
      fListenThread(-1),
      fClientThread(-1),
      fRunning(false)
{
}

NetworkServer::~NetworkServer()
{
    Stop();
}

status_t NetworkServer::Start()
{
    if (fRunning)
        return B_OK;

    // Create server socket
    fServerSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (fServerSocket < 0) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return B_ERROR;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(fServerSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind to port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(fPort);

    if (bind(fServerSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Failed to bind to port %d: %s\n", fPort, strerror(errno));
        close(fServerSocket);
        fServerSocket = -1;
        return B_ERROR;
    }

    // Listen for connections
    if (listen(fServerSocket, 1) < 0) {
        fprintf(stderr, "Failed to listen: %s\n", strerror(errno));
        close(fServerSocket);
        fServerSocket = -1;
        return B_ERROR;
    }

    fRunning = true;

    // Start listen thread
    fListenThread = spawn_thread(ListenThreadFunc, "softKM listener",
        B_NORMAL_PRIORITY, this);

    if (fListenThread < 0) {
        fRunning = false;
        close(fServerSocket);
        fServerSocket = -1;
        return B_ERROR;
    }

    resume_thread(fListenThread);

    printf("softKM server listening on port %d\n", fPort);
    return B_OK;
}

void NetworkServer::Stop()
{
    fRunning = false;

    // Close client socket
    if (fClientSocket >= 0) {
        close(fClientSocket);
        fClientSocket = -1;
    }

    // Close server socket
    if (fServerSocket >= 0) {
        close(fServerSocket);
        fServerSocket = -1;
    }

    // Wait for threads
    if (fListenThread >= 0) {
        status_t result;
        wait_for_thread(fListenThread, &result);
        fListenThread = -1;
    }

    if (fClientThread >= 0) {
        status_t result;
        wait_for_thread(fClientThread, &result);
        fClientThread = -1;
    }
}

int32 NetworkServer::ListenThreadFunc(void* data)
{
    NetworkServer* server = (NetworkServer*)data;
    server->AcceptConnections();
    return 0;
}

void NetworkServer::AcceptConnections()
{
    while (fRunning) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);

        int clientSocket = accept(fServerSocket,
            (struct sockaddr*)&clientAddr, &clientLen);

        if (clientSocket < 0) {
            if (fRunning) {
                fprintf(stderr, "Accept failed: %s\n", strerror(errno));
            }
            continue;
        }

        // Close existing client connection
        if (fClientSocket >= 0) {
            close(fClientSocket);
            if (fClientThread >= 0) {
                status_t result;
                wait_for_thread(fClientThread, &result);
            }
        }

        fClientSocket = clientSocket;

        // Set TCP_NODELAY for low latency
        int opt = 1;
        setsockopt(fClientSocket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        printf("Client connected\n");

        // Notify app of connection
        BMessenger messenger(be_app);
        messenger.SendMessage(MSG_CLIENT_CONNECTED);

        // Start client handling thread
        fClientThread = spawn_thread(ClientThreadFunc, "softKM client",
            B_NORMAL_PRIORITY, this);

        if (fClientThread >= 0) {
            resume_thread(fClientThread);
        }
    }
}

int32 NetworkServer::ClientThreadFunc(void* data)
{
    NetworkServer* server = (NetworkServer*)data;
    server->HandleClient(server->fClientSocket);
    return 0;
}

void NetworkServer::HandleClient(int clientSocket)
{
    uint8 buffer[4096];
    size_t bufferOffset = 0;

    while (fRunning && clientSocket >= 0) {
        ssize_t bytesRead = recv(clientSocket, buffer + bufferOffset,
            sizeof(buffer) - bufferOffset, 0);

        if (bytesRead <= 0) {
            if (bytesRead < 0 && errno == EINTR) {
                continue;
            }
            break;  // Connection closed or error
        }

        bufferOffset += bytesRead;

        // Process complete messages
        while (bufferOffset >= sizeof(ProtocolHeader)) {
            ProtocolHeader* header = (ProtocolHeader*)buffer;

            // Validate magic
            if (header->magic != PROTOCOL_MAGIC) {
                fprintf(stderr, "Invalid magic: 0x%04X\n", header->magic);
                bufferOffset = 0;  // Reset buffer
                break;
            }

            // Check if we have the complete message
            size_t messageSize = sizeof(ProtocolHeader) + header->length;
            if (bufferOffset < messageSize) {
                break;  // Wait for more data
            }

            // Process the message
            ProcessMessage(buffer, messageSize);

            // Remove processed message from buffer
            if (bufferOffset > messageSize) {
                memmove(buffer, buffer + messageSize, bufferOffset - messageSize);
            }
            bufferOffset -= messageSize;
        }
    }

    // Client disconnected
    printf("Client disconnected\n");

    BMessenger messenger(be_app);
    messenger.SendMessage(MSG_CLIENT_DISCONNECTED);

    fClientSocket = -1;
}

void NetworkServer::ProcessMessage(const uint8* data, size_t length)
{
    if (length < sizeof(ProtocolHeader))
        return;

    const ProtocolHeader* header = (const ProtocolHeader*)data;
    const uint8* payload = data + sizeof(ProtocolHeader);

    // Debug: log received event type
    static const char* eventNames[] = {
        "unknown", "KEY_DOWN", "KEY_UP", "MOUSE_MOVE", "MOUSE_DOWN",
        "MOUSE_UP", "MOUSE_WHEEL"
    };
    if (header->eventType >= 1 && header->eventType <= 6) {
        printf("[softKM] Received: %s\n", eventNames[header->eventType]);
    } else if (header->eventType == EVENT_CONTROL_SWITCH) {
        printf("[softKM] Received: CONTROL_SWITCH\n");
    } else if (header->eventType == EVENT_HEARTBEAT) {
        // Don't log heartbeats (too frequent)
    }

    switch (header->eventType) {
        case EVENT_KEY_DOWN:
        case EVENT_KEY_UP:
        {
            if (header->length >= sizeof(KeyEventPayload)) {
                const KeyEventPayload* keyPayload = (const KeyEventPayload*)payload;

                if (header->eventType == EVENT_KEY_DOWN) {
                    // Get the UTF-8 bytes following the fixed part
                    const char* bytes = (const char*)(payload + sizeof(KeyEventPayload));
                    fInputInjector->InjectKeyDown(keyPayload->keyCode,
                        MapModifiers(keyPayload->modifiers),
                        bytes, keyPayload->numBytes);
                } else {
                    fInputInjector->InjectKeyUp(keyPayload->keyCode,
                        MapModifiers(keyPayload->modifiers));
                }
            }
            break;
        }

        case EVENT_MOUSE_MOVE:
        {
            if (header->length >= sizeof(MouseMovePayload)) {
                const MouseMovePayload* movePayload = (const MouseMovePayload*)payload;
                fInputInjector->InjectMouseMove(movePayload->x, movePayload->y,
                    movePayload->relative != 0);
            }
            break;
        }

        case EVENT_MOUSE_DOWN:
        {
            if (header->length >= sizeof(MouseButtonPayload)) {
                const MouseButtonPayload* btnPayload = (const MouseButtonPayload*)payload;
                fInputInjector->InjectMouseDown(btnPayload->buttons,
                    btnPayload->x, btnPayload->y);
            }
            break;
        }

        case EVENT_MOUSE_UP:
        {
            if (header->length >= sizeof(MouseButtonPayload)) {
                const MouseButtonPayload* btnPayload = (const MouseButtonPayload*)payload;
                fInputInjector->InjectMouseUp(btnPayload->buttons,
                    btnPayload->x, btnPayload->y);
            }
            break;
        }

        case EVENT_MOUSE_WHEEL:
        {
            if (header->length >= sizeof(MouseWheelPayload)) {
                const MouseWheelPayload* wheelPayload = (const MouseWheelPayload*)payload;
                fInputInjector->InjectMouseWheel(wheelPayload->deltaX,
                    wheelPayload->deltaY);
            }
            break;
        }

        case EVENT_CONTROL_SWITCH:
        {
            if (header->length >= sizeof(ControlSwitchPayload)) {
                const ControlSwitchPayload* switchPayload = (const ControlSwitchPayload*)payload;
                fInputInjector->SetActive(switchPayload->direction == 0);  // 0 = toHaiku
            }
            break;
        }

        case EVENT_HEARTBEAT:
            SendHeartbeatAck();
            break;

        case EVENT_HEARTBEAT_ACK:
            // Ignore
            break;

        default:
            fprintf(stderr, "Unknown event type: 0x%02X\n", header->eventType);
            break;
    }
}

void NetworkServer::SendHeartbeatAck()
{
    if (fClientSocket < 0)
        return;

    ProtocolHeader header;
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;
    header.eventType = EVENT_HEARTBEAT_ACK;
    header.length = 0;

    send(fClientSocket, &header, sizeof(header), 0);
}

#ifndef CLIPBOARD_MANAGER_H
#define CLIPBOARD_MANAGER_H

#include <SupportDefs.h>

class NetworkServer;

class ClipboardManager {
public:
    ClipboardManager();
    ~ClipboardManager();

    // Get clipboard data for syncing (returns nullptr if empty/too large)
    // Caller owns the returned buffer and must delete[] it
    uint8* GetClipboardForSync(uint32* outLength);

    // Set clipboard from received sync data
    void SetClipboardFromSync(uint8 contentType, const uint8* data, uint32 length);

    void SetNetworkServer(NetworkServer* server) { fNetworkServer = server; }

private:
    NetworkServer* fNetworkServer;

    static const uint32 kMaxClipboardSize = 1048576;  // 1MB
};

#endif // CLIPBOARD_MANAGER_H

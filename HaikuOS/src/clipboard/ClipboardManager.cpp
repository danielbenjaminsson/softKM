#include "ClipboardManager.h"
#include "../Logger.h"

#include <Clipboard.h>
#include <Message.h>
#include <String.h>

#include <cstring>

ClipboardManager::ClipboardManager()
    : fNetworkServer(nullptr)
{
}

ClipboardManager::~ClipboardManager()
{
}

uint8* ClipboardManager::GetClipboardForSync(uint32* outLength)
{
    *outLength = 0;

    if (!be_clipboard->Lock()) {
        LOG("ClipboardManager: Failed to lock clipboard");
        return nullptr;
    }

    BMessage* clip = be_clipboard->Data();
    if (clip == nullptr) {
        be_clipboard->Unlock();
        LOG("ClipboardManager: No clipboard data");
        return nullptr;
    }

    const char* textData = nullptr;
    ssize_t textLength = 0;

    if (clip->FindData("text/plain", B_MIME_TYPE,
            (const void**)&textData, &textLength) != B_OK) {
        be_clipboard->Unlock();
        LOG("ClipboardManager: No text/plain data in clipboard");
        return nullptr;
    }

    be_clipboard->Unlock();

    if (textLength <= 0 || textData == nullptr) {
        LOG("ClipboardManager: Empty clipboard");
        return nullptr;
    }

    if ((uint32)textLength > kMaxClipboardSize) {
        LOG("ClipboardManager: Clipboard too large to sync: %ld bytes", textLength);
        return nullptr;
    }

    uint8* buffer = new uint8[textLength];
    memcpy(buffer, textData, textLength);
    *outLength = (uint32)textLength;

    LOG("ClipboardManager: Got clipboard for sync: %lu bytes", *outLength);
    return buffer;
}

void ClipboardManager::SetClipboardFromSync(uint8 contentType, const uint8* data, uint32 length)
{
    if (contentType != 0x00) {
        LOG("ClipboardManager: Unsupported content type: %d", contentType);
        return;
    }

    if (length > kMaxClipboardSize) {
        LOG("ClipboardManager: Received clipboard too large: %lu bytes", length);
        return;
    }

    if (!be_clipboard->Lock()) {
        LOG("ClipboardManager: Failed to lock clipboard for writing");
        return;
    }

    be_clipboard->Clear();

    BMessage* clip = be_clipboard->Data();
    if (clip != nullptr) {
        clip->AddData("text/plain", B_MIME_TYPE, data, length);
        be_clipboard->Commit();
        LOG("ClipboardManager: Clipboard updated from macOS: %lu bytes", length);
    } else {
        LOG("ClipboardManager: Failed to get clipboard data message");
    }

    be_clipboard->Unlock();
}

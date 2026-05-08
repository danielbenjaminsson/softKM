#include "ClipboardManager.h"
#include "../Logger.h"

#include <Clipboard.h>
#include <Message.h>

#include <cstring>

ClipboardManager::ClipboardManager()
{
}

ClipboardManager::~ClipboardManager()
{
}

uint8* ClipboardManager::GetClipboardForSync(uint32* outLength)
{
    *outLength = 0;

    if (!be_clipboard->Lock()) {
        LOG("ClipboardManager: failed to lock clipboard");
        return nullptr;
    }

    BMessage* clip = be_clipboard->Data();
    if (!clip) { be_clipboard->Unlock(); return nullptr; }

    const char* textData = nullptr;
    ssize_t     textLen  = 0;

    if (clip->FindData("text/plain", B_MIME_TYPE,
            (const void**)&textData, &textLen) != B_OK) {
        be_clipboard->Unlock();
        return nullptr;
    }

    be_clipboard->Unlock();

    if (textLen <= 0 || !textData) return nullptr;
    if ((uint32)textLen > kMaxClipboardSize) {
        LOG("ClipboardManager: too large to sync (%ld bytes)", textLen);
        return nullptr;
    }

    uint8* buf = new uint8[textLen];
    memcpy(buf, textData, textLen);
    *outLength = (uint32)textLen;
    return buf;
}

void ClipboardManager::SetClipboardFromSync(uint8 contentType,
                                             const uint8* data, uint32 length)
{
    if (contentType != 0x00) return;
    if (length > kMaxClipboardSize) return;

    if (!be_clipboard->Lock()) {
        LOG("ClipboardManager: failed to lock for write");
        return;
    }

    be_clipboard->Clear();
    BMessage* clip = be_clipboard->Data();
    if (clip) {
        clip->AddData("text/plain", B_MIME_TYPE, data, length);
        be_clipboard->Commit();
        LOG("ClipboardManager: updated from remote (%lu bytes)", length);
    }
    be_clipboard->Unlock();
}

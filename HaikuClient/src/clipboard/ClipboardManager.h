#ifndef CLIPBOARD_MANAGER_H
#define CLIPBOARD_MANAGER_H

#include <SupportDefs.h>

class ClipboardManager {
public:
    ClipboardManager();
    ~ClipboardManager();

    // Returns heap-allocated buffer; caller must delete[].
    // Sets *outLength to the number of bytes.
    uint8* GetClipboardForSync(uint32* outLength);

    void   SetClipboardFromSync(uint8 contentType,
                                const uint8* data, uint32 length);

private:
    static const uint32 kMaxClipboardSize = 1 * 1024 * 1024;  // 1 MB
};

#endif // CLIPBOARD_MANAGER_H

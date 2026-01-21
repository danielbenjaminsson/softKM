import Cocoa

class ClipboardManager {
    static let shared = ClipboardManager()

    private static let maxClipboardSize = 1_048_576  // 1MB

    private init() {}

    /// Get current clipboard text as Data for syncing (returns nil if empty or too large)
    func getClipboardForSync() -> Data? {
        let pasteboard = NSPasteboard.general
        guard let text = pasteboard.string(forType: .string) else {
            return nil
        }
        guard let data = text.data(using: .utf8) else {
            return nil
        }
        guard data.count <= Self.maxClipboardSize else {
            LOG("Clipboard too large to sync: \(data.count) bytes")
            return nil
        }
        return data
    }

    /// Set clipboard from received sync data
    func setClipboardFromSync(contentType: UInt8, data: Data) {
        guard contentType == 0x00 else {
            LOG("Unsupported clipboard content type: \(contentType)")
            return
        }
        guard let text = String(data: data, encoding: .utf8) else {
            LOG("Failed to decode clipboard text from Haiku")
            return
        }

        let pasteboard = NSPasteboard.general
        pasteboard.clearContents()
        pasteboard.setString(text, forType: .string)
        LOG("Clipboard updated from Haiku: \(text.count) characters")
    }
}

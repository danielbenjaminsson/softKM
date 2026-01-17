import Foundation

class Logger {
    static let shared = Logger()

    private let logFileURL: URL
    private let fileHandle: FileHandle?
    private let dateFormatter: DateFormatter

    private init() {
        dateFormatter = DateFormatter()
        dateFormatter.dateFormat = "HH:mm:ss.SSS"

        // Log to user's tmp directory
        let tmpDir = FileManager.default.temporaryDirectory
        logFileURL = tmpDir.appendingPathComponent("softKM.log")

        // Create or clear the log file
        FileManager.default.createFile(atPath: logFileURL.path, contents: nil, attributes: nil)
        fileHandle = try? FileHandle(forWritingTo: logFileURL)
        fileHandle?.seekToEndOfFile()

        log("=== softKM started ===")
        log("Log file: \(logFileURL.path)")
    }

    deinit {
        log("=== softKM stopped ===")
        try? fileHandle?.close()
    }

    func log(_ message: String, file: String = #file, function: String = #function, line: Int = #line) {
        let timestamp = dateFormatter.string(from: Date())
        let fileName = URL(fileURLWithPath: file).lastPathComponent
        let logMessage = "[\(timestamp)] [\(fileName):\(line)] \(message)\n"

        // Write to file
        if let data = logMessage.data(using: .utf8) {
            fileHandle?.write(data)
            try? fileHandle?.synchronize()
        }

        // Also print to console for debugging
        print(logMessage, terminator: "")
    }
}

// Global convenience function
func LOG(_ message: String, file: String = #file, function: String = #function, line: Int = #line) {
    Logger.shared.log(message, file: file, function: function, line: line)
}

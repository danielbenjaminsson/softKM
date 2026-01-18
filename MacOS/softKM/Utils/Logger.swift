import Foundation
import Combine

class Logger: ObservableObject {
    static let shared = Logger()

    @Published var logEntries: [String] = []
    private let maxEntries = 1000

    private let logFileURL: URL
    private let fileHandle: FileHandle?
    private let dateFormatter: DateFormatter
    private let queue = DispatchQueue(label: "com.softkm.logger")

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
        let logMessage = "[\(timestamp)] [\(fileName):\(line)] \(message)"

        // Write to file
        if let data = (logMessage + "\n").data(using: .utf8) {
            fileHandle?.write(data)
            try? fileHandle?.synchronize()
        }

        // Also print to console for debugging
        print(logMessage)

        // Add to observable log entries on main thread
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.logEntries.append(logMessage)
            // Keep only the last maxEntries
            if self.logEntries.count > self.maxEntries {
                self.logEntries.removeFirst(self.logEntries.count - self.maxEntries)
            }
        }
    }

    func clear() {
        DispatchQueue.main.async { [weak self] in
            self?.logEntries.removeAll()
        }
    }
}

// Global convenience function
func LOG(_ message: String, file: String = #file, function: String = #function, line: Int = #line) {
    Logger.shared.log(message, file: file, function: function, line: line)
}

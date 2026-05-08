#ifndef LOGGER_H
#define LOGGER_H

// Lightweight logger for softKMClient.
// - stderr (always)
// - LogWindow via BMessenger (when set)
// - on-disk file next to the binary (when OpenNextToBinary is called)

#include <Messenger.h>
#include <OS.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>

class LogWindow;

class Logger {
public:
    static Logger& Instance() {
        static Logger sInstance;
        return sInstance;
    }

    void SetLogWindow(BMessenger messenger) { fLogWindow = messenger; }

    // Open a log file next to the running binary (e.g.
    // .../objects.x86_64-cc13-release/softKMClient.log).
    void OpenNextToBinary(const char* binaryPath) {
        char logPath[1024];
        strncpy(logPath, binaryPath, sizeof(logPath) - 1);
        logPath[sizeof(logPath) - 1] = '\0';

        char* lastSlash = strrchr(logPath, '/');
        if (lastSlash) {
            strcpy(lastSlash + 1, "softKMClient.log");
        } else {
            strcpy(logPath, "softKMClient.log");
        }

        Open(logPath);
    }

    void Open(const char* path) {
        if (fFile) fclose(fFile);
        fFile = fopen(path, "a");
        if (fFile) {
            Log("=== softKMClient started (log: %s) ===", path);
        }
    }

    void Close() {
        if (fFile) {
            Log("=== softKMClient stopped ===");
            fclose(fFile);
            fFile = nullptr;
        }
    }

    void Log(const char* fmt, ...) {
        // Format the user message
        char msgBuffer[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msgBuffer, sizeof(msgBuffer), fmt, args);
        va_end(args);

        // Timestamp
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        char timeStr[16];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm_info);

        char logEntry[2200];
        snprintf(logEntry, sizeof(logEntry), "[%s] %s", timeStr, msgBuffer);

        // stderr — useful when launched from a terminal
        fprintf(stderr, "[softKMClient] %s\n", logEntry);

        // File — survives session, can be tail-ed
        if (fFile) {
            fprintf(fFile, "%s\n", logEntry);
            fflush(fFile);
        }

        // LogWindow — send asynchronously (non-blocking) so it never
        // stalls the network/event threads.
        if (fLogWindow.IsValid()) {
            BMessage msg('logM');
            msg.AddString("text", logEntry);
            fLogWindow.SendMessage(&msg, (BHandler*)nullptr, 0);
        }
    }

private:
    Logger() : fFile(nullptr) {}
    ~Logger() { Close(); }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    FILE*      fFile;
    BMessenger fLogWindow;
};

#define LOG(fmt, ...) Logger::Instance().Log(fmt, ##__VA_ARGS__)

#endif // LOGGER_H

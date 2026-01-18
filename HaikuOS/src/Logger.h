#ifndef LOGGER_H
#define LOGGER_H

#include <SupportDefs.h>
#include <Messenger.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <cstring>

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void SetLogWindow(BMessenger messenger) {
        fLogWindowMessenger = messenger;
    }

    void OpenNextToBinary(const char* binaryPath) {
        // Extract directory from binary path
        char logPath[1024];
        strncpy(logPath, binaryPath, sizeof(logPath) - 1);
        logPath[sizeof(logPath) - 1] = '\0';

        // Find last '/' and replace filename with log filename
        char* lastSlash = strrchr(logPath, '/');
        if (lastSlash) {
            strcpy(lastSlash + 1, "softKM.log");
        } else {
            strcpy(logPath, "softKM.log");
        }

        Open(logPath);
    }

    void Open(const char* path) {
        if (fFile) {
            fclose(fFile);
        }
        fFile = fopen(path, "a");
        if (fFile) {
            Log("=== softKM started (log: %s) ===", path);
        }
    }

    void Close() {
        if (fFile) {
            Log("=== softKM stopped ===");
            fclose(fFile);
            fFile = nullptr;
        }
    }

    void Log(const char* format, ...) {
        // Get timestamp
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm_info);

        // Format message
        char msgBuffer[2048];
        va_list args;
        va_start(args, format);
        vsnprintf(msgBuffer, sizeof(msgBuffer), format, args);
        va_end(args);

        // Create full log entry
        char logEntry[2200];
        snprintf(logEntry, sizeof(logEntry), "[%s] %s", timeStr, msgBuffer);

        // Write to file
        if (fFile) {
            fprintf(fFile, "%s\n", logEntry);
            fflush(fFile);
        }

        // Print to stdout
        printf("%s\n", logEntry);

        // Send to log window if available (async to avoid blocking network thread)
        if (fLogWindowMessenger.IsValid()) {
            BMessage msg('LWae');  // LOG_WINDOW_ADD_ENTRY
            msg.AddString("entry", logEntry);
            // Use SendMessage with timeout=0 for non-blocking send
            fLogWindowMessenger.SendMessage(&msg, (BHandler*)NULL, 0);
        }
    }

private:
    Logger() : fFile(nullptr) {}
    ~Logger() { Close(); }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    FILE* fFile;
    BMessenger fLogWindowMessenger;
};

#define LOG(fmt, ...) Logger::Instance().Log(fmt, ##__VA_ARGS__)

#endif // LOGGER_H

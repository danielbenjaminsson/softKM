#ifndef LOGGER_H
#define LOGGER_H

#include <SupportDefs.h>
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
        if (!fFile) return;

        // Get timestamp
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        char timeStr[32];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm_info);

        // Write timestamp
        fprintf(fFile, "[%s] ", timeStr);

        // Write message
        va_list args;
        va_start(args, format);
        vfprintf(fFile, format, args);
        va_end(args);

        fprintf(fFile, "\n");
        fflush(fFile);

        // Also print to stdout for debugging
        printf("[%s] ", timeStr);
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        printf("\n");
    }

private:
    Logger() : fFile(nullptr) {}
    ~Logger() { Close(); }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    FILE* fFile;
};

#define LOG(fmt, ...) Logger::Instance().Log(fmt, ##__VA_ARGS__)

#endif // LOGGER_H

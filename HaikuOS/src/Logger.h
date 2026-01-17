#ifndef LOGGER_H
#define LOGGER_H

#include <SupportDefs.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void Open(const char* path) {
        if (fFile) {
            fclose(fFile);
        }
        fFile = fopen(path, "a");
        if (fFile) {
            Log("=== softKM started ===");
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

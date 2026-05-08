#ifndef LOGGER_H
#define LOGGER_H

// Lightweight logger — identical API to HaikuOS/src/Logger.h
// Writes to a LogWindow via BMessenger when one is set.

#include <Messenger.h>
#include <OS.h>
#include <cstdio>
#include <cstdarg>

class LogWindow;

class Logger {
public:
    static Logger& Instance() {
        static Logger sInstance;
        return sInstance;
    }

    void SetLogWindow(BMessenger messenger) { fLogWindow = messenger; }

    void Log(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        fprintf(stderr, "[softKMClient] %s\n", buf);

        if (fLogWindow.IsValid()) {
            BMessage msg('logM');
            msg.AddString("text", buf);
            fLogWindow.SendMessage(&msg);
        }
    }

private:
    Logger() {}
    BMessenger fLogWindow;
};

#define LOG(fmt, ...) Logger::Instance().Log(fmt, ##__VA_ARGS__)

#endif // LOGGER_H

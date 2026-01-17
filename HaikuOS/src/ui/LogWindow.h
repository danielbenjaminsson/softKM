#ifndef LOG_WINDOW_H
#define LOG_WINDOW_H

#include <Window.h>
#include <TextView.h>
#include <ScrollView.h>
#include <String.h>
#include <Locker.h>

class LogWindow : public BWindow {
public:
    static LogWindow* GetInstance();
    static void DestroyInstance();

    void AddLogEntry(const char* entry);
    void Clear();

    virtual void MessageReceived(BMessage* message);
    virtual bool QuitRequested();

private:
    LogWindow();
    virtual ~LogWindow();

    static LogWindow* sInstance;
    static BLocker sLock;

    BTextView* fTextView;
    BScrollView* fScrollView;
};

// Message codes
enum {
    LOG_WINDOW_CLEAR = 'LWcl',
    LOG_WINDOW_ADD_ENTRY = 'LWae',
};

#endif // LOG_WINDOW_H

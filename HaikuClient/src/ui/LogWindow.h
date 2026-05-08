#ifndef LOG_WINDOW_H
#define LOG_WINDOW_H

#include <Window.h>
#include <TextView.h>
#include <ScrollView.h>
#include <String.h>
#include <Locker.h>

class BCheckBox;

// Log entry categories
enum LogCategory {
    LOG_CAT_MOUSE = 0,
    LOG_CAT_KEYS,
    LOG_CAT_COMM,
    LOG_CAT_OTHER,
    LOG_CAT_COUNT
};

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

    LogCategory CategorizeEntry(const char* entry);
    bool ShouldShow(const char* entry);

    static LogWindow* sInstance;
    static BLocker sLock;

    BTextView* fTextView;
    BScrollView* fScrollView;
    BCheckBox* fMouseCheck;
    BCheckBox* fKeysCheck;
    BCheckBox* fCommCheck;
    BCheckBox* fOtherCheck;

    bool fFilters[LOG_CAT_COUNT];
};

// Message codes
enum {
    LOG_WINDOW_CLEAR = 'LWcl',
    LOG_WINDOW_ADD_ENTRY = 'LWae',
    LOG_WINDOW_FILTER_CHANGED = 'LWfc',
};

#endif // LOG_WINDOW_H

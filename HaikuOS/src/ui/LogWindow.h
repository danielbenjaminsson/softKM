#ifndef LOG_WINDOW_H
#define LOG_WINDOW_H

#include <Window.h>
#include <TextView.h>
#include <ScrollView.h>
#include <String.h>
#include <Locker.h>
#include <vector>

class BCheckBox;

// Log entry categories
enum LogCategory {
    LOG_CAT_MOUSE = 0,
    LOG_CAT_KEYS,
    LOG_CAT_COMM,
    LOG_CAT_OTHER,
    LOG_CAT_COUNT
};

struct LogEntry {
    BString text;
    LogCategory category;
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
    void RefreshDisplay();

    static LogWindow* sInstance;
    static BLocker sLock;

    BTextView* fTextView;
    BScrollView* fScrollView;
    BCheckBox* fMouseCheck;
    BCheckBox* fKeysCheck;
    BCheckBox* fCommCheck;
    BCheckBox* fOtherCheck;

    std::vector<LogEntry> fEntries;
    bool fFilters[LOG_CAT_COUNT];
    bool fInitialized;
};

// Message codes
enum {
    LOG_WINDOW_CLEAR = 'LWcl',
    LOG_WINDOW_ADD_ENTRY = 'LWae',
    LOG_WINDOW_FILTER_CHANGED = 'LWfc',
};

#endif // LOG_WINDOW_H

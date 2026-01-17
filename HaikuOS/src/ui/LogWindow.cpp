#include "LogWindow.h"

#include <Application.h>
#include <Autolock.h>
#include <Button.h>
#include <CheckBox.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <StringView.h>

#include <cstring>
#include <cctype>

LogWindow* LogWindow::sInstance = nullptr;
BLocker LogWindow::sLock("LogWindowLock");

LogWindow* LogWindow::GetInstance()
{
    BAutolock lock(sLock);
    if (sInstance == nullptr) {
        sInstance = new LogWindow();
    }
    return sInstance;
}

void LogWindow::DestroyInstance()
{
    BAutolock lock(sLock);
    if (sInstance != nullptr) {
        if (sInstance->Lock()) {
            sInstance->Quit();
        }
        sInstance = nullptr;
    }
}

static BPath GetSettingsPath()
{
    BPath path;
    find_directory(B_USER_SETTINGS_DIRECTORY, &path);
    path.Append("softKM_logwindow");
    return path;
}

LogWindow::LogWindow()
    : BWindow(BRect(100, 100, 700, 500), "softKM Log",
        B_TITLED_WINDOW,
        B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
    // Initialize filters (all enabled by default)
    for (int i = 0; i < LOG_CAT_COUNT; i++) {
        fFilters[i] = true;
    }

    // Restore saved frame
    BFile file(GetSettingsPath().Path(), B_READ_ONLY);
    if (file.InitCheck() == B_OK) {
        BMessage settings;
        if (settings.Unflatten(&file) == B_OK) {
            BRect frame;
            if (settings.FindRect("frame", &frame) == B_OK) {
                MoveTo(frame.LeftTop());
                ResizeTo(frame.Width(), frame.Height());
            }
        }
    }

    // Create text view for log content
    BRect textRect(0, 0, 580, 350);
    fTextView = new BTextView("logText");
    fTextView->MakeEditable(false);
    fTextView->MakeSelectable(true);
    fTextView->SetStylable(false);

    // Use monospace font
    BFont font(be_fixed_font);
    font.SetSize(11.0);
    fTextView->SetFontAndColor(&font);

    // Wrap in scroll view
    fScrollView = new BScrollView("scrollView", fTextView,
        B_WILL_DRAW | B_FRAME_EVENTS, true, true);

    // Create buttons
    BButton* closeButton = new BButton("close", "Close",
        new BMessage(B_QUIT_REQUESTED));
    closeButton->MakeDefault(true);

    BButton* clearButton = new BButton("clear", "Clear",
        new BMessage(LOG_WINDOW_CLEAR));

    // Create filter checkboxes
    BMessage* mouseMsg = new BMessage(LOG_WINDOW_FILTER_CHANGED);
    mouseMsg->AddInt32("category", LOG_CAT_MOUSE);
    fMouseCheck = new BCheckBox("mouseCheck", "Mouse", mouseMsg);
    fMouseCheck->SetValue(B_CONTROL_ON);

    BMessage* keysMsg = new BMessage(LOG_WINDOW_FILTER_CHANGED);
    keysMsg->AddInt32("category", LOG_CAT_KEYS);
    fKeysCheck = new BCheckBox("keysCheck", "Keys", keysMsg);
    fKeysCheck->SetValue(B_CONTROL_ON);

    BMessage* commMsg = new BMessage(LOG_WINDOW_FILTER_CHANGED);
    commMsg->AddInt32("category", LOG_CAT_COMM);
    fCommCheck = new BCheckBox("commCheck", "Comm", commMsg);
    fCommCheck->SetValue(B_CONTROL_ON);

    BMessage* otherMsg = new BMessage(LOG_WINDOW_FILTER_CHANGED);
    otherMsg->AddInt32("category", LOG_CAT_OTHER);
    fOtherCheck = new BCheckBox("otherCheck", "Other", otherMsg);
    fOtherCheck->SetValue(B_CONTROL_ON);

    // Layout
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .SetInsets(B_USE_WINDOW_SPACING)
        .Add(fScrollView, 1.0)
        .AddGroup(B_HORIZONTAL)
            .Add(closeButton)
            .Add(clearButton)
            .AddGlue()
            .Add(fMouseCheck)
            .Add(fKeysCheck)
            .Add(fCommCheck)
            .Add(fOtherCheck)
        .End()
    .End();

    // Remember position
    SetFlags(Flags() | B_CLOSE_ON_ESCAPE);
}

LogWindow::~LogWindow()
{
}

// Helper to check if string contains substring (case-insensitive)
static bool ContainsIgnoreCase(const char* haystack, const char* needle)
{
    if (!haystack || !needle) return false;

    size_t hLen = strlen(haystack);
    size_t nLen = strlen(needle);
    if (nLen > hLen) return false;

    for (size_t i = 0; i <= hLen - nLen; i++) {
        bool match = true;
        for (size_t j = 0; j < nLen; j++) {
            if (tolower(haystack[i + j]) != tolower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

LogCategory LogWindow::CategorizeEntry(const char* entry)
{
    if (ContainsIgnoreCase(entry, "mouse") ||
        ContainsIgnoreCase(entry, "scroll") ||
        ContainsIgnoreCase(entry, "click") ||
        ContainsIgnoreCase(entry, "cursor") ||
        ContainsIgnoreCase(entry, "wheel")) {
        return LOG_CAT_MOUSE;
    } else if (ContainsIgnoreCase(entry, "key") ||
               ContainsIgnoreCase(entry, "keyboard") ||
               ContainsIgnoreCase(entry, "modifier")) {
        return LOG_CAT_KEYS;
    } else if (ContainsIgnoreCase(entry, "connect") ||
               ContainsIgnoreCase(entry, "disconnect") ||
               ContainsIgnoreCase(entry, "send") ||
               ContainsIgnoreCase(entry, "receive") ||
               ContainsIgnoreCase(entry, "network") ||
               ContainsIgnoreCase(entry, "client") ||
               ContainsIgnoreCase(entry, "server") ||
               ContainsIgnoreCase(entry, "socket") ||
               ContainsIgnoreCase(entry, "tcp") ||
               ContainsIgnoreCase(entry, "heartbeat")) {
        return LOG_CAT_COMM;
    }
    return LOG_CAT_OTHER;
}

void LogWindow::RefreshDisplay()
{
    fTextView->SetText("");

    for (const auto& entry : fEntries) {
        if (fFilters[entry.category]) {
            fTextView->Insert(fTextView->TextLength(), entry.text.String(),
                entry.text.Length());
            fTextView->Insert(fTextView->TextLength(), "\n", 1);
        }
    }

    // Scroll to bottom
    fTextView->ScrollToOffset(fTextView->TextLength());
}

void LogWindow::AddLogEntry(const char* entry)
{
    if (LockLooper()) {
        // Store entry with category
        LogEntry logEntry;
        logEntry.text = entry;
        logEntry.category = CategorizeEntry(entry);
        fEntries.push_back(logEntry);

        // Limit stored entries
        if (fEntries.size() > 2000) {
            fEntries.erase(fEntries.begin(), fEntries.begin() + 500);
        }

        // Only display if filter allows
        if (fFilters[logEntry.category]) {
            fTextView->Insert(fTextView->TextLength(), entry, strlen(entry));
            fTextView->Insert(fTextView->TextLength(), "\n", 1);

            // Scroll to bottom
            fTextView->ScrollToOffset(fTextView->TextLength());

            // Limit display size
            int32 textLen = fTextView->TextLength();
            if (textLen > 50000) {
                RefreshDisplay();
            }
        }

        UnlockLooper();
    }
}

void LogWindow::Clear()
{
    if (LockLooper()) {
        fEntries.clear();
        fTextView->SetText("");
        UnlockLooper();
    }
}

void LogWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case LOG_WINDOW_CLEAR:
            Clear();
            break;

        case LOG_WINDOW_ADD_ENTRY:
        {
            const char* entry;
            if (message->FindString("entry", &entry) == B_OK) {
                AddLogEntry(entry);
            }
            break;
        }

        case LOG_WINDOW_FILTER_CHANGED:
        {
            int32 category;
            if (message->FindInt32("category", &category) == B_OK &&
                category >= 0 && category < LOG_CAT_COUNT) {
                // Get checkbox state
                BCheckBox* checkBox = nullptr;
                switch (category) {
                    case LOG_CAT_MOUSE: checkBox = fMouseCheck; break;
                    case LOG_CAT_KEYS: checkBox = fKeysCheck; break;
                    case LOG_CAT_COMM: checkBox = fCommCheck; break;
                    case LOG_CAT_OTHER: checkBox = fOtherCheck; break;
                }
                if (checkBox) {
                    fFilters[category] = (checkBox->Value() == B_CONTROL_ON);
                    RefreshDisplay();
                }
            }
            break;
        }

        default:
            BWindow::MessageReceived(message);
            break;
    }
}

bool LogWindow::QuitRequested()
{
    // Save frame before hiding
    BMessage settings;
    settings.AddRect("frame", Frame());

    BFile file(GetSettingsPath().Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (file.InitCheck() == B_OK) {
        settings.Flatten(&file);
    }

    // Just hide, don't quit
    Hide();
    return false;
}

#include "LogWindow.h"
#include "../Logger.h"

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

    // Create filter checkboxes (filters apply to new entries only)
    fMouseCheck = new BCheckBox("mouseCheck", "Mouse", nullptr);
    fMouseCheck->SetValue(B_CONTROL_ON);

    fKeysCheck = new BCheckBox("keysCheck", "Keys", nullptr);
    fKeysCheck->SetValue(B_CONTROL_ON);

    fCommCheck = new BCheckBox("commCheck", "Comm", nullptr);
    fCommCheck->SetValue(B_CONTROL_ON);

    fOtherCheck = new BCheckBox("otherCheck", "Other", nullptr);
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

bool LogWindow::ShouldShow(const char* entry)
{
    // Read current checkbox states
    fFilters[LOG_CAT_MOUSE] = (fMouseCheck->Value() == B_CONTROL_ON);
    fFilters[LOG_CAT_KEYS] = (fKeysCheck->Value() == B_CONTROL_ON);
    fFilters[LOG_CAT_COMM] = (fCommCheck->Value() == B_CONTROL_ON);
    fFilters[LOG_CAT_OTHER] = (fOtherCheck->Value() == B_CONTROL_ON);

    LogCategory cat = CategorizeEntry(entry);
    return fFilters[cat];
}

void LogWindow::AddLogEntry(const char* entry)
{
    // Check if this entry should be shown based on current filters
    if (!ShouldShow(entry))
        return;

    // Insert the entry
    fTextView->Insert(fTextView->TextLength(), entry, strlen(entry));
    fTextView->Insert(fTextView->TextLength(), "\n", 1);

    // Scroll to bottom
    fTextView->ScrollToOffset(fTextView->TextLength());

    // Limit log size (keep last ~40000 characters)
    int32 textLen = fTextView->TextLength();
    if (textLen > 50000) {
        fTextView->Delete(0, textLen - 40000);
    }

    // Force redraw
    fTextView->Invalidate();
}

void LogWindow::Clear()
{
    fTextView->SetText("");
    fTextView->Invalidate();
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

    // Disable logging when window is hidden
    Logger::Instance().SetEnabled(false);

    // Just hide, don't quit
    Hide();
    return false;
}

void LogWindow::Show()
{
    // Enable logging when window is shown
    Logger::Instance().SetEnabled(true);
    BWindow::Show();
}

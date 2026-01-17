#include "LogWindow.h"

#include <Application.h>
#include <Autolock.h>
#include <Button.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <StringView.h>

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

    // Layout
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .SetInsets(B_USE_WINDOW_SPACING)
        .Add(fScrollView, 1.0)
        .AddGroup(B_HORIZONTAL)
            .Add(closeButton)
            .Add(clearButton)
            .AddGlue()
        .End()
    .End();

    // Remember position
    SetFlags(Flags() | B_CLOSE_ON_ESCAPE);
}

LogWindow::~LogWindow()
{
}

void LogWindow::AddLogEntry(const char* entry)
{
    if (LockLooper()) {
        // Add the entry
        fTextView->Insert(fTextView->TextLength(), entry, strlen(entry));
        fTextView->Insert(fTextView->TextLength(), "\n", 1);

        // Scroll to bottom
        fTextView->ScrollToOffset(fTextView->TextLength());

        // Limit log size (keep last 10000 characters)
        int32 textLen = fTextView->TextLength();
        if (textLen > 50000) {
            fTextView->Delete(0, textLen - 40000);
        }

        UnlockLooper();
    }
}

void LogWindow::Clear()
{
    if (LockLooper()) {
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

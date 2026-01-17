#include "LogWindow.h"

#include <Application.h>
#include <Autolock.h>
#include <Button.h>
#include <LayoutBuilder.h>
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

LogWindow::LogWindow()
    : BWindow(BRect(100, 100, 700, 500), "softKM Log",
        B_TITLED_WINDOW,
        B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
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

    // Create clear button
    BButton* clearButton = new BButton("clear", "Clear",
        new BMessage(LOG_WINDOW_CLEAR));

    // Layout
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .SetInsets(B_USE_WINDOW_SPACING)
        .Add(fScrollView, 1.0)
        .AddGroup(B_HORIZONTAL)
            .AddGlue()
            .Add(clearButton)
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
    // Just hide, don't quit
    Hide();
    return false;
}

#include "DeskbarReplicant.h"
#include "../SoftKMApp.h"

#include <Dragger.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Roster.h>
#include <Messenger.h>
#include <Window.h>

#include <cstring>

// Icon data (simple 16x16 icons)
static const uint8 kConnectedIconData[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x1f, 0x1f, 0x1f, 0x1f, 0x00, 0x00,
    0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00,
    0x00, 0x1f, 0x00, 0x3f, 0x3f, 0x00, 0x1f, 0x00,
    0x00, 0x1f, 0x00, 0x3f, 0x3f, 0x00, 0x1f, 0x00,
    0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00,
    0x00, 0x00, 0x1f, 0x1f, 0x1f, 0x1f, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8 kDisconnectedIconData[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0f, 0x0f, 0x0f, 0x0f, 0x00, 0x00,
    0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00,
    0x00, 0x0f, 0x00, 0x0f, 0x0f, 0x00, 0x0f, 0x00,
    0x00, 0x0f, 0x00, 0x0f, 0x0f, 0x00, 0x0f, 0x00,
    0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00,
    0x00, 0x00, 0x0f, 0x0f, 0x0f, 0x0f, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

DeskbarReplicant::DeskbarReplicant(BRect frame, const char* name)
    : BView(frame, name, B_FOLLOW_ALL, B_WILL_DRAW),
      fConnectedIcon(nullptr),
      fDisconnectedIcon(nullptr),
      fIsConnected(false),
      fDragger(nullptr)
{
    Init();
}

DeskbarReplicant::DeskbarReplicant(BMessage* archive)
    : BView(archive),
      fConnectedIcon(nullptr),
      fDisconnectedIcon(nullptr),
      fIsConnected(false),
      fDragger(nullptr)
{
    Init();
}

DeskbarReplicant::~DeskbarReplicant()
{
    delete fConnectedIcon;
    delete fDisconnectedIcon;
}

void DeskbarReplicant::Init()
{
    CreateIcons();
}

void DeskbarReplicant::CreateIcons()
{
    // Create simple color icons
    BRect iconRect(0, 0, 15, 15);

    fConnectedIcon = new BBitmap(iconRect, B_RGBA32);
    fDisconnectedIcon = new BBitmap(iconRect, B_RGBA32);

    // Fill with colors (green for connected, gray for disconnected)
    uint32* connBits = (uint32*)fConnectedIcon->Bits();
    uint32* discBits = (uint32*)fDisconnectedIcon->Bits();

    // Green color (connected)
    uint32 green = 0xFF00CC00;
    uint32 darkGreen = 0xFF008800;

    // Gray color (disconnected)
    uint32 gray = 0xFF888888;
    uint32 darkGray = 0xFF666666;

    // Transparent
    uint32 transparent = 0x00000000;

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int idx = y * 16 + x;

            // Create a simple keyboard-like shape
            bool isBorder = (y == 2 || y == 13) && (x >= 2 && x <= 13);
            isBorder |= (x == 2 || x == 13) && (y >= 2 && y <= 13);
            bool isInner = (y >= 5 && y <= 10) && (x >= 5 && x <= 10);

            if (isBorder) {
                connBits[idx] = darkGreen;
                discBits[idx] = darkGray;
            } else if (isInner) {
                connBits[idx] = green;
                discBits[idx] = gray;
            } else {
                connBits[idx] = transparent;
                discBits[idx] = transparent;
            }
        }
    }
}

void DeskbarReplicant::AttachedToWindow()
{
    BView::AttachedToWindow();

    if (Parent() != nullptr) {
        SetViewColor(Parent()->ViewColor());
    } else {
        SetViewColor(B_TRANSPARENT_COLOR);
    }

    SetLowColor(ViewColor());
}

status_t DeskbarReplicant::Archive(BMessage* archive, bool deep) const
{
    status_t status = BView::Archive(archive, deep);
    if (status != B_OK)
        return status;

    // Add app signature for replicant loading
    status = archive->AddString("add_on", "application/x-vnd.softKM");
    if (status != B_OK)
        return status;

    status = archive->AddString("class", "DeskbarReplicant");
    return status;
}

BArchivable* DeskbarReplicant::Instantiate(BMessage* archive)
{
    if (!validate_instantiation(archive, "DeskbarReplicant"))
        return nullptr;
    return new DeskbarReplicant(archive);
}

void DeskbarReplicant::Draw(BRect updateRect)
{
    SetDrawingMode(B_OP_ALPHA);

    BBitmap* icon = fIsConnected ? fConnectedIcon : fDisconnectedIcon;
    if (icon != nullptr) {
        DrawBitmap(icon, BPoint(0, 0));
    }
}

void DeskbarReplicant::MouseDown(BPoint where)
{
    BPoint screenWhere = ConvertToScreen(where);

    uint32 buttons;
    GetMouse(&where, &buttons);

    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        ShowPopUpMenu(screenWhere);
    } else if (buttons & B_PRIMARY_MOUSE_BUTTON) {
        // Double-click to show settings
        int32 clicks = 1;
        if (Window() != nullptr) {
            BMessage* msg = Window()->CurrentMessage();
            if (msg != nullptr) {
                msg->FindInt32("clicks", &clicks);
            }
        }

        if (clicks >= 2) {
            // Launch or activate the app and show settings
            be_roster->Launch("application/x-vnd.softKM");

            BMessenger messenger("application/x-vnd.softKM");
            if (messenger.IsValid()) {
                messenger.SendMessage(MSG_SHOW_SETTINGS);
            }
        }
    }
}

void DeskbarReplicant::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_CONNECTION_STATUS:
        {
            bool connected;
            if (message->FindBool("connected", &connected) == B_OK) {
                SetConnected(connected);
            }
            break;
        }

        case MSG_SHOW_SETTINGS:
        {
            BMessenger messenger("application/x-vnd.softKM");
            if (messenger.IsValid()) {
                messenger.SendMessage(MSG_SHOW_SETTINGS);
            }
            break;
        }

        case MSG_TOGGLE_LOG:
        {
            BMessenger messenger("application/x-vnd.softKM");
            if (messenger.IsValid()) {
                messenger.SendMessage(MSG_TOGGLE_LOG);
            }
            break;
        }

        case MSG_QUIT_REQUESTED:
        {
            BMessenger messenger("application/x-vnd.softKM");
            if (messenger.IsValid()) {
                messenger.SendMessage(B_QUIT_REQUESTED);
            }
            break;
        }

        default:
            BView::MessageReceived(message);
            break;
    }
}

void DeskbarReplicant::SetConnected(bool connected)
{
    if (fIsConnected != connected) {
        fIsConnected = connected;
        Invalidate();
    }
}

void DeskbarReplicant::ShowPopUpMenu(BPoint where)
{
    BPopUpMenu* menu = new BPopUpMenu("softKM", false, false);

    // Status item (disabled, just for info)
    BMenuItem* statusItem = new BMenuItem(
        fIsConnected ? "Connected" : "Disconnected", nullptr);
    statusItem->SetEnabled(false);
    menu->AddItem(statusItem);

    menu->AddSeparatorItem();

    // Show/Hide Log (toggle based on current state)
    BMessage logQuery(MSG_QUERY_LOG_VISIBLE);
    BMessage reply;
    BMessenger messenger("application/x-vnd.softKM");
    bool logVisible = false;
    if (messenger.IsValid() && messenger.SendMessage(&logQuery, &reply, 500000, 500000) == B_OK) {
        reply.FindBool("visible", &logVisible);
    }

    BMenuItem* logItem = new BMenuItem(logVisible ? "Hide Log" : "Show Log",
        new BMessage(MSG_TOGGLE_LOG));
    menu->AddItem(logItem);

    // Settings
    BMenuItem* settingsItem = new BMenuItem("Settings...",
        new BMessage(MSG_SHOW_SETTINGS));
    menu->AddItem(settingsItem);

    menu->AddSeparatorItem();

    // Quit
    BMenuItem* quitItem = new BMenuItem("Quit",
        new BMessage(MSG_QUIT_REQUESTED));
    menu->AddItem(quitItem);

    menu->SetTargetForItems(this);
    menu->Go(where, true, true, true);
}

// Export function for Deskbar
extern "C" _EXPORT BView* instantiate_deskbar_item()
{
    return new DeskbarReplicant(BRect(0, 0, 15, 15), REPLICANT_NAME);
}

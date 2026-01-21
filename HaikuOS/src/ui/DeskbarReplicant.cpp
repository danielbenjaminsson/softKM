#include "DeskbarReplicant.h"
#include "../SoftKMApp.h"

#include <Dragger.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Roster.h>
#include <Messenger.h>
#include <Window.h>
#include <IconUtils.h>
#include <Resources.h>
#include <File.h>

#include <cstring>

DeskbarReplicant::DeskbarReplicant(BRect frame, const char* name)
    : BView(frame, name, B_FOLLOW_ALL, B_WILL_DRAW),
      fConnectedIcon(nullptr),
      fDisconnectedIcon(nullptr),
      fIsConnected(false),
      fDragger(nullptr),
      fStatusPoller(nullptr)
{
    Init();
}

DeskbarReplicant::DeskbarReplicant(BMessage* archive)
    : BView(archive),
      fConnectedIcon(nullptr),
      fDisconnectedIcon(nullptr),
      fIsConnected(false),
      fDragger(nullptr),
      fStatusPoller(nullptr)
{
    Init();
}

DeskbarReplicant::~DeskbarReplicant()
{
    delete fStatusPoller;
    delete fConnectedIcon;
    delete fDisconnectedIcon;
}

void DeskbarReplicant::Init()
{
    CreateIcons();
}

void DeskbarReplicant::CreateIcons()
{
    // Create 16x16 RGBA icons
    BRect iconRect(0, 0, 15, 15);

    fConnectedIcon = new BBitmap(iconRect, B_RGBA32);
    fDisconnectedIcon = new BBitmap(iconRect, B_RGBA32);

    // Try to load HVIF icon from app resources
    entry_ref ref;
    if (be_roster->FindApp("application/x-vnd.softKM", &ref) == B_OK) {
        BFile file(&ref, B_READ_ONLY);
        BResources resources(&file);

        size_t size;
        const void* data = resources.LoadResource(B_VECTOR_ICON_TYPE, "BEOS:ICON", &size);

        if (data != nullptr && size > 0) {
            // Render connected icon (full color)
            BIconUtils::GetVectorIcon((const uint8*)data, size, fConnectedIcon);

            // Render disconnected icon, then convert to grayscale
            BIconUtils::GetVectorIcon((const uint8*)data, size, fDisconnectedIcon);

            // Convert disconnected icon to grayscale
            uint8* bits = (uint8*)fDisconnectedIcon->Bits();
            int32 length = fDisconnectedIcon->BitsLength();

            for (int32 i = 0; i < length; i += 4) {
                // B_RGBA32: bytes are B, G, R, A
                uint8 b = bits[i];
                uint8 g = bits[i + 1];
                uint8 r = bits[i + 2];
                // Luminance formula
                uint8 gray = (uint8)(0.299 * r + 0.587 * g + 0.114 * b);
                bits[i] = gray;
                bits[i + 1] = gray;
                bits[i + 2] = gray;
                // Alpha stays the same
            }
            return;
        }
    }

    // Fallback: solid color icons if HVIF loading fails
    memset(fConnectedIcon->Bits(), 0x54, fConnectedIcon->BitsLength());
    memset(fDisconnectedIcon->Bits(), 0x88, fDisconnectedIcon->BitsLength());
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

    // Start polling for connection status every second
    if (fStatusPoller == nullptr) {
        BMessage pollMsg(MSG_POLL_STATUS);
        fStatusPoller = new BMessageRunner(BMessenger(this),
            &pollMsg, 1000000);  // 1 second interval
    }

    // Query status immediately
    QueryConnectionStatus();
}

void DeskbarReplicant::DetachedFromWindow()
{
    delete fStatusPoller;
    fStatusPoller = nullptr;

    BView::DetachedFromWindow();
}

void DeskbarReplicant::QueryConnectionStatus()
{
    BMessenger appMessenger("application/x-vnd.softKM");
    if (appMessenger.IsValid()) {
        BMessage query(MSG_QUERY_CONNECTION_STATUS);
        BMessage reply;
        if (appMessenger.SendMessage(&query, &reply, 500000, 500000) == B_OK) {
            bool connected;
            if (reply.FindBool("connected", &connected) == B_OK) {
                SetConnected(connected);
            }
        }
    }
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
        case MSG_POLL_STATUS:
            QueryConnectionStatus();
            break;

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

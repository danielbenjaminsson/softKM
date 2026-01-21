#include "DeskbarReplicant.h"
#include "../SoftKMApp.h"

#include <Dragger.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Roster.h>
#include <Messenger.h>
#include <Window.h>

#include <cstring>

// 16x16 icon pattern for μ (mu) symbol on rounded square
// 0 = transparent, 1 = background (green), 2 = symbol (white), 3 = border
static const uint8 kIconPattern[16][16] = {
    {0,0,0,3,3,3,3,3,3,3,3,3,3,0,0,0},
    {0,0,3,1,1,1,1,1,1,1,1,1,1,3,0,0},
    {0,3,1,1,1,1,1,1,1,1,1,1,1,1,3,0},
    {3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3},
    {3,1,1,2,2,1,1,1,1,2,2,1,1,1,1,3},
    {3,1,1,2,2,1,1,1,1,2,2,1,1,1,1,3},
    {3,1,1,2,2,1,1,1,1,2,2,1,1,1,1,3},
    {3,1,1,2,2,1,1,1,1,2,2,1,1,1,1,3},
    {3,1,1,2,2,1,1,1,1,2,2,1,1,1,1,3},
    {3,1,1,2,2,2,1,1,2,2,2,1,1,1,1,3},
    {3,1,1,1,2,2,2,2,2,2,2,1,1,1,1,3},
    {3,1,1,1,1,2,2,2,2,1,2,2,1,1,1,3},
    {3,1,1,1,1,1,1,1,1,1,2,2,1,1,1,3},
    {0,3,1,1,1,1,1,1,1,1,2,2,1,1,3,0},
    {0,0,3,1,1,1,1,1,1,1,1,1,1,3,0,0},
    {0,0,0,3,3,3,3,3,3,3,3,3,3,0,0,0}
};

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
    // Create 16x16 RGBA icons with μ symbol
    BRect iconRect(0, 0, 15, 15);

    fConnectedIcon = new BBitmap(iconRect, B_RGBA32);
    fDisconnectedIcon = new BBitmap(iconRect, B_RGBA32);

    uint32* connBits = (uint32*)fConnectedIcon->Bits();
    uint32* discBits = (uint32*)fDisconnectedIcon->Bits();

    // Colors as 0xAARRGGBB (B_RGBA32 on little-endian stores B,G,R,A in memory)
    // Connected: Green background with white μ symbol (matching macOS icon #54C784)
    uint32 connBg      = 0xFF54C784;  // Green background #54C784
    uint32 connBorder  = 0xFF327749;  // Darker green border
    uint32 connSymbol  = 0xFFFFFFFF;  // White μ symbol

    // Disconnected: Gray background with light gray μ symbol
    uint32 discBg      = 0xFF888888;  // Gray background
    uint32 discBorder  = 0xFF666666;  // Darker gray border
    uint32 discSymbol  = 0xFFDDDDDD;  // Light gray μ symbol

    uint32 transparent = 0x00000000;

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            int idx = y * 16 + x;
            uint8 pattern = kIconPattern[y][x];

            switch (pattern) {
                case 0:  // Transparent
                    connBits[idx] = transparent;
                    discBits[idx] = transparent;
                    break;
                case 1:  // Background
                    connBits[idx] = connBg;
                    discBits[idx] = discBg;
                    break;
                case 2:  // Symbol (μ)
                    connBits[idx] = connSymbol;
                    discBits[idx] = discSymbol;
                    break;
                case 3:  // Border
                    connBits[idx] = connBorder;
                    discBits[idx] = discBorder;
                    break;
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

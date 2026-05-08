#include "DeskbarReplicant.h"
#include "../SoftKMClientApp.h"

#include <Dragger.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Roster.h>
#include <Window.h>
#include <IconUtils.h>
#include <Resources.h>
#include <File.h>

#include <algorithm>
#include <cstring>

DeskbarReplicant::DeskbarReplicant(BRect frame, const char* name)
    : BView(frame, name, B_FOLLOW_ALL, B_WILL_DRAW),
      fConnectedIcon(nullptr),
      fCapturingIcon(nullptr),
      fDisconnectedIcon(nullptr),
      fIsConnected(false),
      fIsCapturing(false),
      fPoller(nullptr)
{
    Init();
}

DeskbarReplicant::DeskbarReplicant(BMessage* archive)
    : BView(archive),
      fConnectedIcon(nullptr),
      fCapturingIcon(nullptr),
      fDisconnectedIcon(nullptr),
      fIsConnected(false),
      fIsCapturing(false),
      fPoller(nullptr)
{
    Init();
}

DeskbarReplicant::~DeskbarReplicant()
{
    delete fPoller;
    delete fConnectedIcon;
    delete fCapturingIcon;
    delete fDisconnectedIcon;
}

void DeskbarReplicant::Init()
{
    CreateIcons();
}

void DeskbarReplicant::CreateIcons()
{
    BRect iconRect(0, 0, 15, 15);

    fConnectedIcon    = new BBitmap(iconRect, B_RGBA32);
    fCapturingIcon    = new BBitmap(iconRect, B_RGBA32);
    fDisconnectedIcon = new BBitmap(iconRect, B_RGBA32);

    // Try to load HVIF icon from app resources
    entry_ref ref;
    if (be_roster->FindApp("application/x-vnd.softKMClient", &ref) == B_OK) {
        BFile file(&ref, B_READ_ONLY);
        BResources resources(&file);
        size_t size;
        const void* data = resources.LoadResource(B_VECTOR_ICON_TYPE,
                                                  "BEOS:ICON", &size);
        if (data && size > 0) {
            BIconUtils::GetVectorIcon((const uint8*)data, size, fConnectedIcon);
            BIconUtils::GetVectorIcon((const uint8*)data, size, fCapturingIcon);
            BIconUtils::GetVectorIcon((const uint8*)data, size, fDisconnectedIcon);

            // Tint capturing icon (add blue overlay feel — just lighten it)
            uint8* bits = (uint8*)fCapturingIcon->Bits();
            int32 len   = fCapturingIcon->BitsLength();
            for (int32 i = 0; i < len; i += 4) {
                // B_RGBA32: B G R A  — boost blue channel
                bits[i]     = (uint8)std::min(255, (int)bits[i]   + 60);   // B
                bits[i + 2] = (uint8)std::max(0,   (int)bits[i+2] - 30);   // R
            }

            // Greyscale disconnected icon
            bits = (uint8*)fDisconnectedIcon->Bits();
            len  = fDisconnectedIcon->BitsLength();
            for (int32 i = 0; i < len; i += 4) {
                uint8 gray = (uint8)(0.299f * bits[i+2] +
                                     0.587f * bits[i+1] +
                                     0.114f * bits[i]);
                bits[i] = bits[i+1] = bits[i+2] = gray;
            }
            return;
        }
    }

    // Fallback solid colours
    memset(fConnectedIcon->Bits(),    0x54, fConnectedIcon->BitsLength());
    memset(fCapturingIcon->Bits(),    0x34, fCapturingIcon->BitsLength());
    memset(fDisconnectedIcon->Bits(), 0x88, fDisconnectedIcon->BitsLength());
}

void DeskbarReplicant::AttachedToWindow()
{
    BView::AttachedToWindow();
    SetViewColor(Parent() ? Parent()->ViewColor() : B_TRANSPARENT_COLOR);
    SetLowColor(ViewColor());

    if (!fPoller) {
        BMessage poll(MSG_POLL_STATUS);
        fPoller = new BMessageRunner(BMessenger(this), &poll, 1000000);
    }
    QueryStatus();
}

void DeskbarReplicant::DetachedFromWindow()
{
    delete fPoller;
    fPoller = nullptr;
    BView::DetachedFromWindow();
}

void DeskbarReplicant::QueryStatus()
{
    BMessenger app("application/x-vnd.softKMClient");
    if (!app.IsValid()) {
        SetConnected(false, false);
        return;
    }
    BMessage q(MSG_QUERY_STATUS);
    BMessage r;
    if (app.SendMessage(&q, &r, 500000, 500000) == B_OK) {
        bool connected = false, capturing = false;
        r.FindBool("connected", &connected);
        r.FindBool("capturing", &capturing);
        SetConnected(connected, capturing);
    }
}

status_t DeskbarReplicant::Archive(BMessage* archive, bool deep) const
{
    status_t s = BView::Archive(archive, deep);
    if (s != B_OK) return s;
    archive->AddString("add_on", "application/x-vnd.softKMClient");
    archive->AddString("class",  "DeskbarReplicant");
    return B_OK;
}

BArchivable* DeskbarReplicant::Instantiate(BMessage* archive)
{
    if (!validate_instantiation(archive, "DeskbarReplicant"))
        return nullptr;
    return new DeskbarReplicant(archive);
}

void DeskbarReplicant::Draw(BRect /*updateRect*/)
{
    SetDrawingMode(B_OP_ALPHA);
    BBitmap* icon = fIsCapturing  ? fCapturingIcon
                  : fIsConnected  ? fConnectedIcon
                                  : fDisconnectedIcon;
    if (icon) DrawBitmap(icon, BPoint(0, 0));
}

void DeskbarReplicant::MouseDown(BPoint where)
{
    uint32 buttons;
    BPoint dummy = where;
    GetMouse(&dummy, &buttons);

    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        ShowPopUpMenu(ConvertToScreen(where));
    } else {
        int32 clicks = 1;
        if (Window() && Window()->CurrentMessage())
            Window()->CurrentMessage()->FindInt32("clicks", &clicks);
        if (clicks >= 2) {
            be_roster->Launch("application/x-vnd.softKMClient");
            BMessenger m("application/x-vnd.softKMClient");
            if (m.IsValid()) m.SendMessage(MSG_SHOW_SETTINGS);
        }
    }
}

void DeskbarReplicant::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case MSG_POLL_STATUS:
            QueryStatus();
            break;

        case MSG_SHOW_SETTINGS:
        {
            BMessenger m("application/x-vnd.softKMClient");
            if (m.IsValid()) m.SendMessage(MSG_SHOW_SETTINGS);
            break;
        }
        case MSG_TOGGLE_LOG:
        {
            BMessenger m("application/x-vnd.softKMClient");
            if (m.IsValid()) m.SendMessage(MSG_TOGGLE_LOG);
            break;
        }
        case MSG_SHOW_ABOUT:
        {
            BMessenger m("application/x-vnd.softKMClient");
            if (m.IsValid()) m.SendMessage(MSG_SHOW_ABOUT);
            break;
        }
        case MSG_QUIT_REQUESTED:
        {
            BMessenger m("application/x-vnd.softKMClient");
            if (m.IsValid()) m.SendMessage(B_QUIT_REQUESTED);
            break;
        }
        default:
            BView::MessageReceived(message);
    }
}

void DeskbarReplicant::SetConnected(bool connected, bool capturing)
{
    if (fIsConnected != connected || fIsCapturing != capturing) {
        fIsConnected = connected;
        fIsCapturing = capturing;
        Invalidate();
    }
}

void DeskbarReplicant::ShowPopUpMenu(BPoint where)
{
    BPopUpMenu* menu = new BPopUpMenu("softKMClient", false, false);

    // Status
    const char* statusStr =
        fIsCapturing  ? "Capturing — forwarding to right Haiku"
      : fIsConnected  ? "Connected"
                      : "Disconnected";
    BMenuItem* si = new BMenuItem(statusStr, nullptr);
    si->SetEnabled(false);
    menu->AddItem(si);
    menu->AddSeparatorItem();

    // Log toggle
    BMessenger app("application/x-vnd.softKMClient");
    BMessage logQ(MSG_TOGGLE_LOG);   // we just toggle; label is approximate
    menu->AddItem(new BMenuItem("Show/Hide Log",
        new BMessage(MSG_TOGGLE_LOG)));

    // Settings
    menu->AddItem(new BMenuItem("Settings" B_UTF8_ELLIPSIS,
        new BMessage(MSG_SHOW_SETTINGS)));

    // About
    menu->AddItem(new BMenuItem("About softKMClient" B_UTF8_ELLIPSIS,
        new BMessage(MSG_SHOW_ABOUT)));

    menu->AddSeparatorItem();
    menu->AddItem(new BMenuItem("Quit", new BMessage(MSG_QUIT_REQUESTED)));

    menu->SetTargetForItems(this);
    menu->Go(where, true, true, true);
}

extern "C" _EXPORT BView* instantiate_deskbar_item()
{
    return new DeskbarReplicant(BRect(0, 0, 15, 15), REPLICANT_NAME);
}

#ifndef DESKBAR_REPLICANT_H
#define DESKBAR_REPLICANT_H

#include <View.h>
#include <Bitmap.h>
#include <MessageRunner.h>

#define REPLICANT_NAME "softKM"

// Message for polling connection status
enum {
    MSG_POLL_STATUS = 'poll'
};

class BDragger;
class BPopUpMenu;

class DeskbarReplicant : public BView {
public:
    // Constructor for normal creation
    DeskbarReplicant(BRect frame, const char* name);

    // Constructor for replicant instantiation from archive
    DeskbarReplicant(BMessage* archive);

    virtual ~DeskbarReplicant();

    // BArchivable interface
    static BArchivable* Instantiate(BMessage* archive);
    virtual status_t Archive(BMessage* archive, bool deep = true) const override;

    // BView interface
    virtual void Draw(BRect updateRect) override;
    virtual void MouseDown(BPoint where) override;
    virtual void MessageReceived(BMessage* message) override;
    virtual void AttachedToWindow() override;
    virtual void DetachedFromWindow() override;

    void SetConnected(bool connected);

private:
    void Init();
    void CreateIcons();
    void ShowPopUpMenu(BPoint where);
    void QueryConnectionStatus();

    BBitmap* fConnectedIcon;
    BBitmap* fDisconnectedIcon;
    bool fIsConnected;
    BDragger* fDragger;
    BMessageRunner* fStatusPoller;
};

// Required export for replicant instantiation
extern "C" _EXPORT BView* instantiate_deskbar_item();

#endif // DESKBAR_REPLICANT_H

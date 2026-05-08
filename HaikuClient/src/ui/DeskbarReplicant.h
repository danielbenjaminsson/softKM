#ifndef DESKBAR_REPLICANT_H
#define DESKBAR_REPLICANT_H

#include <View.h>
#include <Bitmap.h>
#include <MessageRunner.h>
#include <Archivable.h>

class DeskbarReplicant : public BView {
public:
    DeskbarReplicant(BRect frame, const char* name);
    DeskbarReplicant(BMessage* archive);
    virtual ~DeskbarReplicant();

    static BArchivable* Instantiate(BMessage* archive);
    virtual status_t    Archive(BMessage* archive, bool deep = true) const;

    virtual void AttachedToWindow();
    virtual void DetachedFromWindow();
    virtual void Draw(BRect updateRect);
    virtual void MouseDown(BPoint where);
    virtual void MessageReceived(BMessage* message);

private:
    void Init();
    void CreateIcons();
    void QueryStatus();
    void ShowPopUpMenu(BPoint where);
    void SetConnected(bool connected, bool capturing);

    BBitmap*        fConnectedIcon;
    BBitmap*        fCapturingIcon;
    BBitmap*        fDisconnectedIcon;
    bool            fIsConnected;
    bool            fIsCapturing;
    BMessageRunner* fPoller;
};

extern "C" _EXPORT BView* instantiate_deskbar_item();

#endif // DESKBAR_REPLICANT_H

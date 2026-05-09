#ifndef SWITCH_CONTROLLER_H
#define SWITCH_CONTROLLER_H

#include <OS.h>
#include <SupportDefs.h>
#include <Point.h>

class NetworkClient;
class ClipboardManager;

// ------------------------------------------------------------------
// SwitchController
//
// Mirrors macOS SwitchController.swift — watches mouse position for
// edge dwell and drives the capture / release cycle.
//
// In MONITORING mode  — events flow normally to the desktop.
// In CAPTURING  mode  — events are intercepted by the filter add-on
//                       and forwarded here; we encode + send them to
//                       the right-hand Haiku server.
// ------------------------------------------------------------------
class SwitchController {
public:
    enum Mode { kMonitoring, kCapturing };

    SwitchController();
    ~SwitchController();

    void SetNetworkClient(NetworkClient* client) { fClient = client; }
    void SetClipboardManager(ClipboardManager* mgr) { fClipboard = mgr; }

    // Called from the main app's message loop (events arrive from the
    // filter add-on via the softkm_filter_event port).
    void HandleFilterEvent(uint32 what, const char* buf, ssize_t size);

    // Called by NetworkClient when the right Haiku sends CONTROL_SWITCH
    // direction=1 (return to us).
    void OnReturnFromRight(float yRatio);

    Mode  GetMode() const { return fMode; }
    bool  IsCapturing() const { return fMode == kCapturing; }

    // Configuration (set from Settings)
    void SetSwitchEdge(uint8 edge)   { fSwitchEdge = edge; }
    void SetReturnEdge(uint8 edge)   { fReturnEdge = edge; }
    void SetDwellTime(float seconds) { fDwellTime = (bigtime_t)(seconds * 1000000LL); }

    // Start/stop the event-port listener thread
    status_t Start();
    void     Stop();

private:
    void ProcessMouseMove(BPoint where, int32 buttons, int32 modifiers,
                          bool hasFilterDelta = false,
                          float filterDx = 0.f, float filterDy = 0.f);
    void ProcessMouseDown(BPoint where, int32 buttons, int32 modifiers, int32 clicks);
    void ProcessMouseUp(BPoint where, int32 buttons, int32 modifiers);
    void ProcessMouseWheel(float dx, float dy, int32 modifiers);
    void ProcessKeyDown(int32 key, int32 modifiers, int32 rawChar, const char* bytes);
    void ProcessKeyUp(int32 key, int32 modifiers);

    void ActivateCapture();
    void DeactivateCapture(float yRatio = 0.5f);

    void LockCursor(BPoint edgePos);
    void UnlockCursor(float yRatio);

    void SendActivateToFilter(BPoint lockedPos);
    void SendDeactivateToFilter();

    // Edge detection helpers
    bool IsAtEdge(BPoint pos, uint8 edge) const;
    BPoint EdgeLockPosition(uint8 edge) const;

    // Mouse-move batching (sent every ~16 ms by a dedicated thread)
    static int32 FlushThreadFunc(void* data);
    void FlushLoop();

    // Mouse position polling thread (monitoring mode — reads cursor pos directly)
    static int32 PollThreadFunc(void* data);
    void PollLoop();

    // Event-port listener thread
    static int32 EventThreadFunc(void* data);
    void EventLoop();

    Mode     fMode;
    uint8    fSwitchEdge;   // edge that triggers switching TO right Haiku
    uint8    fReturnEdge;   // edge that triggers returning FROM right Haiku
    bigtime_t fDwellTime;   // microseconds to dwell before switching

    bool     fAtSwitchEdge;
    bigtime_t fEdgeDwellStart;

    BPoint   fLastMousePos;
    BPoint   fLockedCursorPos;
    int32    fCurrentButtons;
    int32    fCurrentModifiers;

    // Batched relative mouse deltas (accumulated from absolute positions)
    float    fPendingDX;
    float    fPendingDY;
    uint32   fPendingMods;
    bool     fHasPending;
    sem_id   fBatchLock;
    BPoint   fLastSentPos;  // last position we sent so we can compute delta
    bool     fSkipFirstMove; // skip first move after activation — pre-warp pos
                             // would generate a huge bogus delta
    int32    fDeltaLogCount; // how many post-activation deltas we've logged
                             // (capped to keep the log readable)

    NetworkClient*    fClient;
    ClipboardManager* fClipboard;

    port_id   fEventPort;    // softkm_filter_event  (owned by this controller)
    port_id   fCmdPort;      // softkm_filter_cmd    (owned by the filter add-on)

    thread_id fEventThread;
    thread_id fFlushThread;
    thread_id fPollThread;
    volatile bool fRunning;

    // Screen dimensions (for edge detection)
    float fScreenW;
    float fScreenH;
};

#endif // SWITCH_CONTROLLER_H

#ifndef SETTINGS_H
#define SETTINGS_H

#include <SupportDefs.h>

// Application mode. softKM is one binary that runs in either of two
// roles, decided at startup from the saved settings:
//
//   MODE_CLIENT — sender side: captures local input, forwards to a
//                 remote machine. Replaces the legacy macOS app
//                 and the (now retired) softKMClient binary.
//                 Default for new installs.
//   MODE_SERVER — receiver side: listens on a TCP port, injects
//                 incoming events into the local input_server.
//
enum AppMode {
    MODE_CLIENT = 0,
    MODE_SERVER = 1
};

class Settings {
public:
    static void Load();
    static void Save();

    // ---- Mode (which role this binary acts in) ----
    static AppMode GetMode()             { return sMode; }
    static void    SetMode(AppMode m)    { sMode = m; }

    // ---- Common ----
    static uint16 GetPort()              { return sPort; }
    static void   SetPort(uint16 p)      { sPort = p; }

    static bool   GetAutoStart()         { return sAutoStart; }
    static void   SetAutoStart(bool v)   { sAutoStart = v; }

    // ---- Client-only (host to connect to and edge-switch behaviour) ----
    static const char* GetHostAddress()               { return sHostAddress; }
    static void        SetHostAddress(const char* h);

    static float GetDwellTime()          { return sDwellTime; }
    static void  SetDwellTime(float t)   { sDwellTime = t; }

    static uint8 GetSwitchEdge()         { return sSwitchEdge; }
    static void  SetSwitchEdge(uint8 e)  { sSwitchEdge = e; }

    static uint8 GetReturnEdge()         { return sReturnEdge; }
    static void  SetReturnEdge(uint8 e)  { sReturnEdge = e; }

private:
    static AppMode sMode;
    static uint16  sPort;
    static bool    sAutoStart;
    static char    sHostAddress[256];
    static float   sDwellTime;
    static uint8   sSwitchEdge;
    static uint8   sReturnEdge;
};

#endif // SETTINGS_H

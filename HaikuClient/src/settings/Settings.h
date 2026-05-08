#ifndef SETTINGS_H
#define SETTINGS_H

#include <SupportDefs.h>

class Settings {
public:
    static void Load();
    static void Save();

    static uint16 GetPort()        { return sPort; }
    static void   SetPort(uint16 p){ sPort = p; }

    static const char* GetHostAddress()               { return sHostAddress; }
    static void        SetHostAddress(const char* h);

    static float GetDwellTime()          { return sDwellTime; }
    static void  SetDwellTime(float t)   { sDwellTime = t; }

    static uint8 GetSwitchEdge()         { return sSwitchEdge; }
    static void  SetSwitchEdge(uint8 e)  { sSwitchEdge = e; }

    static uint8 GetReturnEdge()         { return sReturnEdge; }
    static void  SetReturnEdge(uint8 e)  { sReturnEdge = e; }

    static bool GetAutoStart()           { return sAutoStart; }
    static void SetAutoStart(bool v)     { sAutoStart = v; }

private:
    static uint16 sPort;
    static char   sHostAddress[256];
    static float  sDwellTime;
    static uint8  sSwitchEdge;
    static uint8  sReturnEdge;
    static bool   sAutoStart;
};

#endif // SETTINGS_H

#ifndef SETTINGS_H
#define SETTINGS_H

#include <SupportDefs.h>

class Settings {
public:
    static void Load();
    static void Save();

    static uint16 GetPort() { return sPort; }
    static void SetPort(uint16 port) { sPort = port; }

    static bool GetAutoStart() { return sAutoStart; }
    static void SetAutoStart(bool autoStart) { sAutoStart = autoStart; }

private:
    static uint16 sPort;
    static bool sAutoStart;
};

#endif // SETTINGS_H

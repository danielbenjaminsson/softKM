#include "Settings.h"

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include <cstdio>
#include <cstring>

// ---- Defaults ------------------------------------------------------
//
// Default mode is CLIENT (the sender role). softKM was originally a
// macOS-only sender talking to a Haiku-only receiver; the unified
// binary now also takes over the sender role on Haiku, replacing
// the legacy softKMClient app. Most fresh installs are intended to
// be the sender (the receiver side is usually a deliberate choice
// once the user has decided which machine to remote-control).
//
AppMode Settings::sMode        = MODE_CLIENT;
uint16  Settings::sPort        = 31337;        // leet!
bool    Settings::sAutoStart   = false;

// Client-only defaults
char    Settings::sHostAddress[256] = "enter computer hostname";
float   Settings::sDwellTime   = 0.3f;
uint8   Settings::sSwitchEdge  = 0;            // EDGE_RIGHT
uint8   Settings::sReturnEdge  = 1;            // EDGE_LEFT

static const char* kSettingsFileName = "softKM_settings";

static BPath GetSettingsPath()
{
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK)
        path.Append(kSettingsFileName);
    return path;
}

void Settings::SetHostAddress(const char* h)
{
    if (h) {
        strncpy(sHostAddress, h, sizeof(sHostAddress) - 1);
        sHostAddress[sizeof(sHostAddress) - 1] = '\0';
    }
}

void Settings::Load()
{
    BPath path = GetSettingsPath();
    if (path.InitCheck() != B_OK)
        return;

    BFile file(path.Path(), B_READ_ONLY);
    if (file.InitCheck() != B_OK)
        return;

    BMessage settings;
    if (settings.Unflatten(&file) != B_OK)
        return;

    int32 mode;
    if (settings.FindInt32("mode", &mode) == B_OK
        && (mode == MODE_CLIENT || mode == MODE_SERVER)) {
        sMode = (AppMode)mode;
    }

    uint16 port;
    if (settings.FindUInt16("port", &port) == B_OK)
        sPort = port;

    bool autoStart;
    if (settings.FindBool("autoStart", &autoStart) == B_OK)
        sAutoStart = autoStart;

    const char* host;
    if (settings.FindString("host", &host) == B_OK)
        SetHostAddress(host);

    float dwell;
    if (settings.FindFloat("dwellTime", &dwell) == B_OK)
        sDwellTime = dwell;

    uint8 edge;
    if (settings.FindUInt8("switchEdge", &edge) == B_OK)
        sSwitchEdge = edge;
    if (settings.FindUInt8("returnEdge", &edge) == B_OK)
        sReturnEdge = edge;

    printf("Settings loaded: mode=%s port=%u host=%s dwell=%.2f "
           "switch=%u return=%u autoStart=%d\n",
        sMode == MODE_CLIENT ? "client" : "server",
        sPort, sHostAddress, sDwellTime, sSwitchEdge, sReturnEdge, sAutoStart);
}

void Settings::Save()
{
    BPath path = GetSettingsPath();
    if (path.InitCheck() != B_OK)
        return;

    BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (file.InitCheck() != B_OK) {
        fprintf(stderr, "Settings: failed to create settings file\n");
        return;
    }

    BMessage settings;
    settings.AddInt32 ("mode",       (int32)sMode);
    settings.AddUInt16("port",       sPort);
    settings.AddBool  ("autoStart",  sAutoStart);
    settings.AddString("host",       sHostAddress);
    settings.AddFloat ("dwellTime",  sDwellTime);
    settings.AddUInt8 ("switchEdge", sSwitchEdge);
    settings.AddUInt8 ("returnEdge", sReturnEdge);

    if (settings.Flatten(&file) != B_OK) {
        fprintf(stderr, "Settings: failed to write\n");
        return;
    }
}

#include "Settings.h"

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include <cstdio>
#include <cstring>

uint16 Settings::sPort         = 31337;
char   Settings::sHostAddress[256] = "taurus.microgeni.synology.me";
float  Settings::sDwellTime    = 0.3f;
uint8  Settings::sSwitchEdge   = 0;  // EDGE_RIGHT
uint8  Settings::sReturnEdge   = 1;  // EDGE_LEFT
bool   Settings::sAutoStart    = false;

static const char* kSettingsFileName = "softKMClient_settings";

static BPath GetSettingsPath()
{
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK)
        path.Append(kSettingsFileName);
    return path;
}

void Settings::SetHostAddress(const char* h)
{
    if (h) strncpy(sHostAddress, h, sizeof(sHostAddress) - 1);
}

void Settings::Load()
{
    BPath path = GetSettingsPath();
    if (path.InitCheck() != B_OK) return;

    BFile file(path.Path(), B_READ_ONLY);
    if (file.InitCheck() != B_OK) return;

    BMessage s;
    if (s.Unflatten(&file) != B_OK) return;

    uint16 port;
    if (s.FindUInt16("port", &port) == B_OK) sPort = port;

    const char* host;
    if (s.FindString("host", &host) == B_OK)
        strncpy(sHostAddress, host, sizeof(sHostAddress) - 1);

    float dwell;
    if (s.FindFloat("dwellTime", &dwell) == B_OK) sDwellTime = dwell;

    uint8 edge;
    if (s.FindUInt8("switchEdge", &edge) == B_OK) sSwitchEdge = edge;
    if (s.FindUInt8("returnEdge", &edge) == B_OK) sReturnEdge = edge;

    bool autoStart;
    if (s.FindBool("autoStart", &autoStart) == B_OK) sAutoStart = autoStart;

    printf("Settings loaded: host=%s port=%u dwell=%.2f switch=%u return=%u\n",
        sHostAddress, sPort, sDwellTime, sSwitchEdge, sReturnEdge);
}

void Settings::Save()
{
    BPath path = GetSettingsPath();
    if (path.InitCheck() != B_OK) return;

    BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (file.InitCheck() != B_OK) return;

    BMessage s;
    s.AddUInt16("port",       sPort);
    s.AddString("host",       sHostAddress);
    s.AddFloat ("dwellTime",  sDwellTime);
    s.AddUInt8 ("switchEdge", sSwitchEdge);
    s.AddUInt8 ("returnEdge", sReturnEdge);
    s.AddBool  ("autoStart",  sAutoStart);

    if (s.Flatten(&file) != B_OK)
        fprintf(stderr, "softKMClient: Failed to write settings\n");
}

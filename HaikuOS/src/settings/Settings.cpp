#include "Settings.h"

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include <cstdio>

// Default values
uint16 Settings::sPort = 31337;  // leet!
bool Settings::sAutoStart = false;

static const char* kSettingsFileName = "softKM_settings";

static BPath GetSettingsPath()
{
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append(kSettingsFileName);
    }
    return path;
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

    uint16 port;
    if (settings.FindUInt16("port", &port) == B_OK) {
        sPort = port;
    }

    bool autoStart;
    if (settings.FindBool("autoStart", &autoStart) == B_OK) {
        sAutoStart = autoStart;
    }

    printf("Settings loaded: port=%d, autoStart=%d\n", sPort, sAutoStart);
}

void Settings::Save()
{
    BPath path = GetSettingsPath();
    if (path.InitCheck() != B_OK)
        return;

    BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (file.InitCheck() != B_OK) {
        fprintf(stderr, "Failed to create settings file\n");
        return;
    }

    BMessage settings;
    settings.AddUInt16("port", sPort);
    settings.AddBool("autoStart", sAutoStart);

    if (settings.Flatten(&file) != B_OK) {
        fprintf(stderr, "Failed to write settings\n");
        return;
    }

    printf("Settings saved: port=%d, autoStart=%d\n", sPort, sAutoStart);
}

#include "SoftKMApp.h"
#include "Logger.h"
#include "Settings.h"

#include <image.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>


// ---------------------------------------------------------------------
// CLI bootstrap mode: --configure
//
// Lets a deploy script set persistent settings without launching the
// full app or driving the GUI. Exits 0 on success, 1 on bad usage.
//
// Usage examples:
//   softKM --configure --mode=client --host=asus.example.com --port=31337
//   softKM --configure --mode=server --port=31337
//
// Any flag that's not present leaves the corresponding setting at its
// previous (or default) value, so this is safe to run repeatedly.
// ---------------------------------------------------------------------
static int RunConfigure(int argc, char** argv)
{
    Settings::Load();   // start from existing values; only override
                        // what the user names on the command line.

    bool changed = false;
    for (int i = 2; i < argc; i++) {
        const char* a = argv[i];
        if (strncmp(a, "--mode=", 7) == 0) {
            const char* v = a + 7;
            if (strcmp(v, "client") == 0) {
                Settings::SetMode(MODE_CLIENT);
            } else if (strcmp(v, "server") == 0) {
                Settings::SetMode(MODE_SERVER);
            } else {
                fprintf(stderr, "softKM --configure: unknown --mode=%s "
                                "(want 'client' or 'server')\n", v);
                return 1;
            }
            changed = true;
        } else if (strncmp(a, "--host=", 7) == 0) {
            Settings::SetHostAddress(a + 7);
            changed = true;
        } else if (strncmp(a, "--port=", 7) == 0) {
            int p = atoi(a + 7);
            if (p > 0 && p < 65536) {
                Settings::SetPort((uint16)p);
                changed = true;
            } else {
                fprintf(stderr, "softKM --configure: bad --port=%s\n", a + 7);
                return 1;
            }
        } else if (strncmp(a, "--switch-edge=", 14) == 0) {
            int e = atoi(a + 14);
            Settings::SetSwitchEdge((uint8)e);
            changed = true;
        } else if (strncmp(a, "--return-edge=", 14) == 0) {
            int e = atoi(a + 14);
            Settings::SetReturnEdge((uint8)e);
            changed = true;
        } else if (strncmp(a, "--dwell=", 8) == 0) {
            float d = (float)atof(a + 8);
            Settings::SetDwellTime(d);
            changed = true;
        } else if (strncmp(a, "--auto-start=", 13) == 0) {
            Settings::SetAutoStart(strcmp(a + 13, "yes") == 0
                                || strcmp(a + 13, "true") == 0
                                || strcmp(a + 13, "1") == 0);
            changed = true;
        } else {
            fprintf(stderr, "softKM --configure: unknown flag '%s'\n", a);
            return 1;
        }
    }

    if (changed)
        Settings::Save();

    fprintf(stderr,
        "softKM settings: mode=%s port=%u host=%s dwell=%.2f "
        "switch=%u return=%u autoStart=%d\n",
        Settings::GetMode() == MODE_CLIENT ? "client" : "server",
        Settings::GetPort(), Settings::GetHostAddress(),
        Settings::GetDwellTime(), Settings::GetSwitchEdge(),
        Settings::GetReturnEdge(), Settings::GetAutoStart());
    return 0;
}


int main(int argc, char** argv)
{
    if (argc >= 2 && strcmp(argv[1], "--configure") == 0)
        return RunConfigure(argc, argv);

    // Get the path to the binary to put log file next to it
    image_info info;
    int32 cookie = 0;
    const char* binaryPath = argv[0];

    // Try to get the actual executable path
    while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
        if (info.type == B_APP_IMAGE) {
            binaryPath = info.name;
            break;
        }
    }

    // Initialize logger next to the binary
    Logger::Instance().OpenNextToBinary(binaryPath);

    SoftKMApp app;
    app.Run();

    Logger::Instance().Close();
    return 0;
}

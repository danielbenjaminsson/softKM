#include "SoftKMClientApp.h"
#include "Logger.h"

#include <image.h>
#include <signal.h>
#include <cstring>

int main(int argc, char** argv)
{
    // Ignore SIGPIPE — when the peer closes the TCP connection while we
    // are mid-send, the kernel would otherwise deliver SIGPIPE, killing
    // the process silently. We handle send() errors by checking the
    // return value instead.
    signal(SIGPIPE, SIG_IGN);

    // Resolve our own binary path so the log file lands next to it.
    image_info info;
    int32 cookie = 0;
    const char* binaryPath = argc > 0 ? argv[0] : "softKMClient";

    while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
        if (info.type == B_APP_IMAGE) {
            binaryPath = info.name;
            break;
        }
    }

    Logger::Instance().OpenNextToBinary(binaryPath);

    SoftKMClientApp app;
    app.Run();

    Logger::Instance().Close();
    return 0;
}

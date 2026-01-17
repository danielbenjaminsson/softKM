#include "SoftKMApp.h"
#include "Logger.h"

#include <image.h>
#include <cstring>

int main(int argc, char** argv)
{
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

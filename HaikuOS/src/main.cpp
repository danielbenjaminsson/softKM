#include "SoftKMApp.h"
#include "Logger.h"

int main()
{
    // Initialize logger - log to /tmp for easy access
    Logger::Instance().Open("/tmp/softKM.log");

    SoftKMApp app;
    app.Run();

    Logger::Instance().Close();
    return 0;
}

// Copyright 2019-2021 Varjo Technologies Oy. All rights reserved.

/* Mixed Reality Example Application
 *
 * - Showcases Varjo MR API features: Camera, data streams, rendering, and more!
 * - Run example and press F1 for help
 */

// Internal includes
#include "Globals.hpp"
#include "AppLogic.hpp"
#include "AppView.hpp"

// Common main function called from the entry point
void commonMain()
{
    // Instantiate application logic and view
    auto appLogic = std::make_unique<AppLogic>();
    auto appView = std::make_unique<AppView>(*appLogic);

    try {
        // Init application
        LOG_DEBUG("Initializing application..");
        if (appView->init()) {
            // Enter the main loop
            LOG_DEBUG("Running application..");
            appView->run();
        } else {
            LOG_ERROR("Initializing application failed.");
        }
    } catch (const std::runtime_error& e) {
        LOG_ERROR("Critical error caught: %s", e.what());
    }

    // Deinitialize client app
    LOG_DEBUG("Deinitializing application..");
    appView.reset();
    appLogic.reset();

    // Exit successfully
    LOG_INFO("Done!");
}

// Console application entry point
int main(int argc, char** argv)
{
    // Call common main function
    commonMain();

    // Application finished
    return EXIT_SUCCESS;
}

// Windows application entry point
int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    int argc = 0;
    LPWSTR* args = CommandLineToArgvW(pCmdLine, &argc);

    bool console = false;
    for (int i = 0; i < argc; i++) {
        if (std::wstring(args[i]) == (L"--console")) {
            console = true;
        }
    }

    if (console) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            errno_t err;
            FILE* stream = nullptr;
            err = freopen_s(&stream, "CONOUT$", "w", stdout);
            err = freopen_s(&stream, "CONOUT$", "w", stderr);
        }
    }

    // Call common main function
    commonMain();

    // Application finished
    return EXIT_SUCCESS;
}

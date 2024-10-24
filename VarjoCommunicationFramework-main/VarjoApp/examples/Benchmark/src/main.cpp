/**
 * Varjo SDK example.
 *
 * main.cpp:
 *      Contains the main rendering loop and all of the frame logic that
 *      is not related to graphics.
 *
 *      This application renders a number of donuts and a background grid
 *      to stress test the Varjo API.
 *
 * IRenderer.hpp:
 *      Contains an abstract renderer class that takes care of actual rendering.
 *      Implementation can be found from D3D11Renderer.hpp and GLRenderer.hpp.
 *
 * Copyright (C) 2018 Varjo Technologies Ltd.
 */

#define GLM_ENABLE_EXPERIMENTAL
#include <atomic>
#include <glm/vec3.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <Varjo.h>
#include <Varjo_mr.h>
#include <Varjo_events.h>
#include <Varjo_layers.h>
#include <Varjo_types_layers.h>

#include <cxxopts.hpp>

#include "OpenVRTracker.hpp"
#include "Profiler.hpp"
#include "GLRenderer.hpp"
#include "D3D11Renderer.hpp"
#include "GeometryGenerator.hpp"
#include "GazeTracking.hpp"
#include "D3D12Renderer.hpp"

#ifdef USE_VULKAN
#include "VKRenderer.hpp"
#endif

#include "Config.hpp"

#include <thread>

namespace
{
enum class RendererType {
    DIRECT3D11,
    DIRECT3D12,
    OPENGL,
    VULKAN,
    UNKNOWN,
};
}

bool gotKey();

struct ObjectRotation {
    glm::vec3 axis;
    float speed;
};

void createObjects(std::shared_ptr<IRenderer> renderer, bool disableAnimation, std::vector<IRenderer::Object>& object, int maxDonuts);

void createController(std::shared_ptr<IRenderer> renderer, IRenderer::Object& controllerObject);
void createGaze(std::shared_ptr<IRenderer> renderer, IRenderer::Object& gazeObject);

static std::atomic_bool s_shouldExit = false;
BOOL WINAPI ctrlHandler(DWORD /*dwCtrlType*/)
{
    s_shouldExit = true;
    return TRUE;
}

int main(int argc, char** argv)
{
    // Exit gracefully, when Ctrl-C signal is received
    SetConsoleCtrlHandler(ctrlHandler, TRUE);

    cxxopts::Options options("Benchmark",
        "Varjo Benchmark Test Client\n"  //
        "(C) 2019-2020 Varjo Technologies");

    options.add_options()                                                                                                                           //
#ifdef USE_VULKAN                                                                                                                                   //
        ("renderer", "Renderer to be used. Defaults to d3d11. Allowed options: <gl|d3d11|d3d12|vulkan>", cxxopts::value<std::string>())             //
#else                                                                                                                                               //
        ("renderer", "Renderer to be used. Defaults to d3d11. Allowed options: <gl|d3d11|d3d12>", cxxopts::value<std::string>())  //
#endif                                                                                                                                              //
        ("use-controllers", "Enables controllers(initializes SteamVR runtime")                                                                      //
        ("disable-animation", "Disable all animation")                                                                                              //
        ("profile-start-frame", "Start profiling after the given frame number", cxxopts::value<int>()->default_value("0"))                          //
        ("profile-frame-count", "Number of frames to profile for. Exits after all frames are profiled", cxxopts::value<int>()->default_value("0"))  //
        ("fps", "Print fps count")                                                                                                                  //
        ("gaze", "Use eye tracking")                                                                                                                //
        ("use-depth", "Enable layer depth buffer (requires layers API)")                                                                            //
        ("vst-render", "Enable video see through rendering in compositor")                                                                          //
        ("vst-depth", "Enable VST depth sorting in compositor (requires layers API and depth)")                                                     //
        ("stereo", "Uses two big textures instead of four. Focus area is cropped from the texture")                                                 //
        ("use-occlusion-mesh", "Render only visible area of the texture. Requires stencil buffer, --depth-format=d24s8|d32s8")                      //
        ("depth-format", "Set depth/stencil buffer format. Defaults to d32. Allowed options: <d32|d24s8|d32s8>", cxxopts::value<std::string>())     //
        ("reverse-depth", "Use reverse depth buffer (d3d11 and d3d12 only)")                                                                        //
        ("use-sli", "Split left and right eye rendering with different gpus (opengl and d3d12 only)")                                               //
        ("use-slave-gpu", "Render both eye views on slave gpu (gpu which is not connected to hmd) (opengl and d3d12 only)")                         //
        ("use-velocity", "Enable layer velocity buffer (requires layers API and depth)")                                                            //
        ("use-foveation", "Use dynamic viewport foveation")                                                                                         //
        ("use-vrs", "Use Variable Rate Shading map")                                                                                                //
        ("visualize-vrs", "Visualize Variable Rate Shading map")                                                                                    //
        ("max-donuts", "Maximum number of donuts allowed to render", cxxopts::value<int>()->default_value("100000"))                                //
        ("no-srgb", "Do not use SRGB texture")                                                                                                      //
        ("show-mirror-window", "Show mirror window")                                                                                                //
        ("draw-always", "Submit frames even when we are not visible")                                                                               //
        ("help", "Display help info");

    try {
        auto arguments = options.parse(argc, argv);

        if (arguments.count("help")) {
            std::cout << options.help();
            return EXIT_SUCCESS;
        }

        std::string rendererName = arguments.count("renderer") ? arguments["renderer"].as<std::string>() : "d3d11";
        bool useControllers = arguments.count("use-controllers");
        bool useReverseDepth = arguments.count("reverse-depth") && rendererName != "gl";
        bool useDepth = arguments.count("use-depth") || useReverseDepth;
        bool disableAnimation = arguments.count("disable-animation");
        bool enableProfiling = arguments.count("profile-start-frame") && arguments.count("profile-frame-count");
        bool printFps = arguments.count("fps");
        bool useGaze = arguments.count("gaze");
        bool useVstRender = arguments.count("vst-render");
        bool useVstDepth = arguments.count("vst-depth");
        bool useStereo = arguments.count("stereo");
        bool useOcclusionMesh = arguments.count("use-occlusion-mesh");
        bool useSli = arguments.count("use-sli");
        bool useSlaveGpu = arguments.count("use-slave-gpu");
        bool useDynamicViewports = arguments.count("use-foveation");
        bool enableVrs = arguments.count("use-vrs");
        bool useVelocity = arguments.count("use-velocity");
        bool noSrgb = arguments.count("no-srgb");
        bool showMirrorWindow = arguments.count("show-mirror-window");
        bool drawAlways = arguments.count("draw-always");
        int maxDonuts = arguments.count("max-donuts") ? arguments["max-donuts"].as<int>() : 100000;
        std::string depthFormatName = arguments.count("depth-format") ? arguments["depth-format"].as<std::string>() : "d32";

        if (useOcclusionMesh && depthFormatName != "d24s8" && depthFormatName != "d32s8") {
            printf("Disabling use of occlusion mesh. --use-occlusion-mesh requires depth format with stencil buffer.");
            useOcclusionMesh = false;
        }
        if (useSli && (rendererName == "d3d11" || rendererName == "vulkan")) {
            printf("Disabling use of sli. --use-sli requires opengl or d3d12.");
            useSli = false;
        }
        if (useSlaveGpu && !useSli) {
            printf("Disabling use of slave gpu. Rendering on slave gpu requires --use-sli.");
            useSlaveGpu = false;
        }
        if (useVelocity && disableAnimation) {
            printf("Disabling velocity. --use-velocity requires animation.");
            useVelocity = false;
        }
        if (useVelocity && !useDepth) {
            printf("Force enabling depth. --use-velocity is not expected to work without depth.");
            useDepth = true;
        }

        printf("Startup params:");
        printf("  Renderer: %s\n", rendererName.c_str());
        printf("  Use depth: %s\n", useDepth ? "enabled" : "disabled");
        printf("  Animation: %s\n", disableAnimation ? "disabled" : "enabled");
        printf("  Profiling: %s\n", enableProfiling ? "enabled" : "disabled");
        printf("  Gaze: %s\n", useGaze ? "enabled" : "disabled");
        printf("  VST rendering: %s\n", useVstRender ? "enabled" : "disabled");
        printf("  VST depth: %s\n", useVstDepth ? "enabled" : "disabled");
        printf("  Occlusion mesh: %s\n", useOcclusionMesh ? "enabled" : "disabled");
        printf("  Depth format: %s\n", depthFormatName.c_str());
        printf("  Use reverse depth: %s\n", useReverseDepth ? "enabled" : "disabled");
        printf("  Use SLI: %s\n", useSli ? "enabled" : "disabled");
        printf("  Use velocity: %s\n", useVelocity ? "enabled" : "disabled");
        printf("  Use SRGB texture format: %s\n", !noSrgb ? "enabled" : "disabled");
        printf("  Show mirror window: %s\n", !showMirrorWindow ? "enabled" : "disabled");

        int32_t profileStartFrame = arguments["profile-start-frame"].as<int>();
        int32_t profileFrameCount = arguments["profile-frame-count"].as<int>();

        if (enableProfiling) {
            printf("Profile:\n");
            printf("  Start frame: %d\n", profileStartFrame);
            printf("  Frame count: %d\n", profileFrameCount);
        }

        Profiler profiler;

        if (!varjo_IsAvailable()) {
            printf("ERROR: Varjo system not available.\n");
            exit(EXIT_FAILURE);
        }

        // Initialize the varjo session
        varjo_Session* session = varjo_SessionInit();

        // Check if there was any errors while initializing.
        varjo_Error error = varjo_GetError(session);
        if (error != varjo_NoError) {
            printf("ERROR: Failed to initialize Varjo session: %s\n", varjo_GetErrorDesc(error));
            exit(EXIT_FAILURE);
        }

        varjo_TextureFormat depthFormat = 0;
        if (depthFormatName == "d32") {
            depthFormat = varjo_DepthTextureFormat_D32_FLOAT;
        } else if (depthFormatName == "d24s8") {
            depthFormat = varjo_DepthTextureFormat_D24_UNORM_S8_UINT;
        } else if (depthFormatName == "d32s8") {
            depthFormat = varjo_DepthTextureFormat_D32_FLOAT_S8_UINT;
        } else {
            printf("ERROR: Unknown depth format: %s\n", depthFormatName.c_str());
            exit(EXIT_FAILURE);
        }

        const bool enableVisualizeVrs = enableVrs && arguments.count("visualize-vrs");

        RendererType rendererType{RendererType::UNKNOWN};
        RendererSettings rendererSettings{useDepth, useVstRender, useVstDepth, useStereo, useOcclusionMesh, depthFormat, useReverseDepth, useSli, useSlaveGpu,
            useDynamicViewports, enableVrs, useGaze, enableVisualizeVrs, useVelocity, noSrgb, showMirrorWindow};

        std::shared_ptr<IRenderer> renderer;
        if (rendererName == "gl") {
            rendererType = RendererType::OPENGL;
            renderer = std::make_shared<GLRenderer>(session, rendererSettings);
        } else if (rendererName == "d3d11") {
            rendererType = RendererType::DIRECT3D11;
            renderer = std::make_shared<D3D11Renderer>(session, rendererSettings);
        } else if (rendererName == "d3d12") {
            rendererType = RendererType::DIRECT3D12;
            renderer = std::make_shared<D3D12Renderer>(session, rendererSettings);
        } else if (rendererName == "vulkan") {
#ifdef USE_VULKAN
            rendererType = RendererType::VULKAN;
            renderer = std::make_shared<VKRenderer>(session, rendererSettings);
#else
            printf("ERROR: Benchmark compiled without Vulkan support\n");
            exit(EXIT_FAILURE);
#endif
        } else {
            printf("ERROR: Unknown renderer: %s\n", rendererName.c_str());
            exit(EXIT_FAILURE);
        }

        const bool vrsEnabledAndSupported = enableVrs && renderer->isVrsSupported();
        if (enableVrs && !vrsEnabledAndSupported) {
            printf("Warning: VRS is not supported\n");
        }
        const bool visualizeVrs = vrsEnabledAndSupported && enableVisualizeVrs;
        printf("  Use VRS: %s\n", vrsEnabledAndSupported ? "enabled" : "disabled");
        printf("  Visualize VRS: %s\n", visualizeVrs ? "enabled" : "disabled");

        // Initialize.
        // Calls varjo_*Init and fetches all swap chain textures.
        if (!renderer->init()) {
            exit(1);
        }

        std::unique_ptr<OpenVRTracker> openVRTracker;
        if (useControllers) {
            openVRTracker = std::make_unique<OpenVRTracker>();
            openVRTracker->init();
        }

        // Initialize gaze tracking when needed
        GazeTracking gaze(session);
        if (useGaze) gaze.init();

        std::vector<IRenderer::Object> donutObjects;
        IRenderer::Object controllerObject;
        IRenderer::Object gazeObject;

        createObjects(renderer, disableAnimation, donutObjects, maxDonuts);
        createController(renderer, controllerObject);
        createGaze(renderer, gazeObject);

        // Initialize mixed reality
        if (useVstRender || useVstDepth) {
            // Check if Mixed Reality hardware is available.
            varjo_Bool mixedRealityAvailable = varjo_False;
            varjo_SyncProperties(session);
            if (varjo_HasProperty(session, varjo_PropertyKey_MRAvailable)) {
                mixedRealityAvailable = varjo_GetPropertyBool(session, varjo_PropertyKey_MRAvailable);
            }

            // Check mixed reality availability
            if (mixedRealityAvailable == varjo_True) {
                printf("Varjo MR available!\n");

                // Enable VST rendering if used
                if (useVstRender) {
                    printf("Enabling VST rendering.\n");
                    varjo_MRSetVideoRender(session, varjo_True);
                }

                // Enable VST depth testing if used
                if (useVstDepth) {
                    // Check for required flags
                    if (!useDepth) {
                        printf("ERROR: Depth is required for VST depth testing\n\n");
                        printf("%s", options.help().c_str());
                        exit(EXIT_FAILURE);
                    }

                    printf("Enabling VST depth occlusion.\n");
                    varjo_MRSetVideoDepthEstimation(session, varjo_True);
                }
            } else {
                printf("ERROR: Varjo MR capabilities not available.\n");
                exit(EXIT_FAILURE);
            }
        }

        // Frame info is used for per-frame view and projection matrices.
        varjo_FrameInfo* frameInfo = varjo_CreateFrameInfo(session);

        // A struct to hold Varjo event data.
        varjo_Event evt{};
        varjo_Nanoseconds lastFrameTime = varjo_GetCurrentTime(session);

        int32_t frameNumber = 0;

        bool visible = true;

        while (!(gotKey() || s_shouldExit)) {
            if (renderer->getWindow()) {
                if (!renderer->getWindow()->runEventLoop()) {
                    s_shouldExit = true;
                }
            }
            // Poll Varjo events.
            while (varjo_PollEvent(session, &evt)) {
                switch (evt.header.type) {
                    case varjo_EventType_Visibility: {
                        // Don't render anything when we are hidden.
                        visible = evt.data.visibility.visible;
                        printf("Visible %s\n", visible ? "true" : "false");
                    } break;
                    case varjo_EventType_Foreground: {
                        printf("In foreground %s\n", evt.data.foreground.isForeground ? "true" : "false");
                    } break;
                    case varjo_EventType_StandbyStatus: {
                        printf("Headset on standby %s\n", evt.data.standbyStatus.onStandby ? "true" : "false");
                    } break;
                    case varjo_EventType_Button: {
                        if (evt.data.button.buttonId == varjo_ButtonId_Application && evt.data.button.pressed) {
                            // Request gaze calibration when button is pressed
                            gaze.requestCalibration();
                        }
                    } break;
                    case varjo_EventType_TextureSizeChange: {
                        printf("Received Event TextureSizeChange (Mask:0x%I64X). Recreating Swapchains.\n", evt.data.textureSizeChange.typeMask);
                        renderer->recreateSwapchains();
                    } break;
                    case varjo_EventType_VisibilityMeshChange: {
                        printf("Visibility mesh changed. Recreating visibility/occlusion mesh for view %d.\n", evt.data.visibilityMeshChange.viewIndex);
                        renderer->recreateOcclusionMesh(evt.data.visibilityMeshChange.viewIndex);
                    }
                }
            }
            if (visible || drawAlways) {
                // Wait for a perfect time to render the frame.
                varjo_WaitSync(session, frameInfo);

                if (openVRTracker) {
                    // Update the tracking position for hand controllers.
                    openVRTracker->update();
                }

                if (enableProfiling && frameNumber >= profileStartFrame) {
                    profiler.addSample();

                    if (profiler.sampleCount() == 0) {
                        printf("Start profiling.\n");
                    }
                }

                float time = (frameInfo->displayTime - lastFrameTime) / 1000000000.0f;

                // Count FPS if enabled
                if (printFps) {
                    profiler.updateFps();
                }

                // Rotate objects
                size_t numObjects = donutObjects.size();
                for (size_t i = 0; i < numObjects; ++i) {
                    IRenderer::applyObjectVelocity(donutObjects[i], time);
                }

                std::vector<IRenderer::Object> controllerObjects;

                const glm::mat4 trackingToLocalMat = glm::make_mat4(varjo_GetTrackingToLocalTransform(session).value);

                if (openVRTracker) {
                    for (int i = 0; i < openVRTracker->getControllerCount(); ++i) {
                        IRenderer::Object newObject = controllerObject;

                        glm::mat4 controllerPose = glm::mat4_cast(openVRTracker->getControllerOrientation(i));
                        const glm::vec3 controllerPosition = openVRTracker->getControllerPosition(i);
                        controllerPose[3] = {controllerPosition.x, controllerPosition.y, controllerPosition.z, 1};

                        const glm::mat4 controllerPoseWithOffset = trackingToLocalMat * controllerPose;

                        newObject.position = glm::make_vec3(controllerPoseWithOffset[3]);
                        newObject.orientation = glm::quat(controllerPoseWithOffset);

                        if (openVRTracker->isButtonTouched(i)) newObject.scale = newObject.scale * glm::vec3{1.5, 1.5, 1.5};
                        if (openVRTracker->isButtonPressed(i)) newObject.scale = newObject.scale * glm::vec3{1.5, 1.5, 1.5};
                        controllerObjects.push_back(newObject);
                    }
                }

                // Add object where user is looking when gaze data is valid
                std::vector<IRenderer::Object> gazeObjects;
                if (gaze.update()) {
                    IRenderer::Object newObject = gazeObject;
                    newObject.position = gaze.getPosition();
                    gazeObjects.push_back(newObject);
                }

                // Render into the swap chain texture.
                renderer->render(frameInfo, {&donutObjects, &controllerObjects, &gazeObjects});

                controllerObjects.clear();
                // Check if we had any errors during the frame
                varjo_Error err = varjo_GetError(session);
                if (err != varjo_NoError) {
                    printf("error: %s\n", varjo_GetErrorDesc(err));
                    break;
                }

                lastFrameTime = frameInfo->displayTime;
                frameNumber++;

                if (enableProfiling && profiler.sampleCount() == profileFrameCount) {
                    printf("Profiling finished.\n");
                    break;
                }
            } else {
                // Sleep explicitly when not drawing. Normally sleep happens during varjo_WaitSync.
                std::this_thread::sleep_for(std::chrono::milliseconds{50});
            }
        }

        if (enableProfiling) {
            profiler.exportCSV("frame_times.csv");
        }

        if (openVRTracker) {
            openVRTracker->exit();
        }

        renderer->finishRendering();
        renderer->freeVarjoResources();

        // Free the frame and submit infos.
        varjo_FreeFrameInfo(frameInfo);

        // We don't need the session anymore
        varjo_SessionShutDown(session);

        // Clean up geometry before shutting down the renderer
        gazeObject.geometry.reset();
        controllerObject.geometry.reset();
        donutObjects.clear();

        // Shut down the renderer
        renderer.reset();
    } catch (const std::exception& e) {
        std::cerr << e.what();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

bool gotKey()
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD input;
    DWORD count = 0;
    while (GetNumberOfConsoleInputEvents(in, &count) && count > 0) {
        if (ReadConsoleInputA(in, &input, 1, &count) && count > 0 && input.EventType == KEY_EVENT && input.Event.KeyEvent.bKeyDown) {
            if (input.Event.KeyEvent.uChar.AsciiChar == '\033') {
                printf("Quit requested.\n");
                return true;
            }
        }
    }
    return false;
}

float randomFloat(float min, float max)
{
    float value = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    return min + (max - min) * value;
}

void createController(std::shared_ptr<IRenderer> renderer, IRenderer::Object& controllerObject)
{
    auto pentagonGeometry = GeometryGenerator::generateDonut(renderer, 0.08f, 0.05f, 5, 3);

    controllerObject.geometry = pentagonGeometry;
    controllerObject.position = glm::vec3{0, 0, 0};
    controllerObject.scale = glm::vec3{1, 1, 1};
    controllerObject.orientation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));

    printf("Created object for hand controllers\n");
}

void createGaze(std::shared_ptr<IRenderer> renderer, IRenderer::Object& gazeObject)
{
    auto circleGeometry = GeometryGenerator::generateDonut(renderer, 0.01f, 0.005f, 32, 16);

    gazeObject.geometry = circleGeometry;
    gazeObject.position = glm::vec3{0, 0, 0};
    gazeObject.scale = glm::vec3{1, 1, 1};
    gazeObject.orientation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));

    printf("Created object for gaze\n");
}

void createObjects(std::shared_ptr<IRenderer> renderer, bool disableAnimation, std::vector<IRenderer::Object>& objects, int maxDonuts)
{
    auto donutGeometry = GeometryGenerator::generateDonut(renderer, 0.25f, 0.125f, 256, 64);

    const int32_t donutCount = 14;
    const int32_t rows = 5;
    const int32_t layers = 20;
    const float rowMin = -1.0f;
    const float rowMax = 2.0f;
    const float layerMin = 0.75f;
    const float layerSize = 2.0f;
    const float angle = 360.0f / static_cast<float>(donutCount);
    const float layerOffsetangle = angle * (1.0f / static_cast<float>(layers));

    srand(123);  // Keep the animations same on each run.

    for (int32_t l = 0; l < layers; ++l) {  // Layers going outward from the center.
        float offsetAngle = l * layerOffsetangle;
        float z = layerMin + (layerSize * static_cast<float>(l));

        for (int32_t r = 0; r < rows; ++r) {  // Rows going up from the bottom.
            float y = rowMin + ((rowMax - rowMin) / static_cast<float>(rows - 1)) * static_cast<float>(r);

            for (int32_t i = 0; i < donutCount; ++i) {  // Number of donuts in a circle.
                auto rotate = glm::angleAxis(glm::radians((angle * static_cast<float>(i)) + offsetAngle), glm::vec3(0, 1, 0));

                IRenderer::Object object{};
                object.geometry = donutGeometry;
                object.position = glm::rotate(rotate, glm::vec3{0, y, z});
                object.scale = glm::vec3{1, 1, 1};
                object.orientation = rotate * glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));

                if (!disableAnimation) {
                    // Random axis of rotation and rotation speed for each object.
                    object.velocity = {
                        glm::vec3{randomFloat(-1.0f, 1.0f), randomFloat(-1.0f, 1.0f), randomFloat(-1.0f, 1.0f)},
                        glm::radians(randomFloat(30.0f, 120.0f)),
                    };
                }

                objects.push_back(object);

                if (objects.size() >= static_cast<size_t>(maxDonuts)) {
                    goto end;
                }
            }
        }
    }

end:
    printf("Created %zu donuts\n", objects.size());
    printf("%zu triangles per frame\n", objects.size() * (donutGeometry->indexCount() / 3));
}

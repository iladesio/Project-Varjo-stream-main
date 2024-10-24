// Copyright 2019-2021 Varjo Technologies Oy. All rights reserved.

#include "AppView.hpp"

#include <algorithm>
#include <unordered_map>
#include <tchar.h>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <imgui_internal.h>
#include <map>
#include <chrono>

// Application title text
#define APP_TITLE_TEXT "Varjo Mixed Reality Example"
#define APP_COPYRIGHT_TEXT "(C) 2019-2021 Varjo Technologies"

// Enable debug frame timing
#define DEBUG_FRAME_TIMING 0

// VarjoExamples namespace contains simple example wrappers for using Varjo API features.
// These are only meant to be used in SDK example applications. In your own application,
// use your own production quality integration layer.
using namespace VarjoExamples;

namespace
{
//! Action info structure
struct ActionInfo {
    std::string name;  //!< Action name
    int keyCode;       //!< Shortcut keycode
    std::string help;  //!< Help string
};

// Key shortcut mapping
std::unordered_map<int, AppView::Action> c_keyMappings;

// Action mappings
// clang-format off
static const std::unordered_map<AppView::Action, ActionInfo> c_actions = {
    {AppView::Action::None,                          {"None",                            -1,         "--    (no action)"}},
    {AppView::Action::Quit,                          {"Quit",                            VK_ESCAPE,  "ESC   Quit"}},
    {AppView::Action::Reset,                         {"Reset",                           -1,         "--    Reset all settings"}},
    {AppView::Action::Help,                          {"Help",                            VK_F1,      "F1    Print help"}},
    {AppView::Action::PrintStreamConfigs,            {"PrintStreamConfigs",              VK_F2,      "F2    Fetch and print stream configs"}},
    {AppView::Action::PrintCameraProperties,         {"PrintCameraProperties",           VK_F3,      "F3    Fetch and print camera properties"}},
    {AppView::Action::PrintCurrentCameraConfig,      {"PrintCurrentCameraConfig",        VK_F4,      "F4    Print current camera config"}},
    {AppView::Action::ToggleVideoDepthEstimationOn,  {"ToggleVideoDepthEstimationOn",    VK_F5,      "F5    Toggle video depth sorting ON"}},
    {AppView::Action::ToggleVideoDepthEstimationOff, {"ToggleVideoDepthEstimationOff",   VK_F6,      "F6    Toggle video depth sorting OFF"}},
    {AppView::Action::ToggleChromaKeying,            {"ToggleChromaKeying",              VK_F7,      "F7    Toggle chroma keying"}},
    {AppView::Action::ToggleVRViewOffset,            {"ToggleVRViewOffset",              VK_F8,      "F8    Toggle VR view offset: 0%, 50%, 100%"}},
    {AppView::Action::ToggleBufferHandlingMode,      {"ToggleBufferHandlingMode",        VK_F9,      "F9    Toggle buffer handling mode"}},
    {AppView::Action::ToggleRenderVideoOn,           {"ToggleRenderVideoOn",             VK_LEFT,    "LEFT  Toggle video rendering ON"}},
    {AppView::Action::ToggleRenderVideoOff,          {"ToggleRenderVideoOff",            VK_RIGHT,   "RIGHT Toggle video rendering OFF"}},
    {AppView::Action::ToggleStreamColorYUV,          {"ToggleStreamColorYUV",            VK_DOWN,    "DOWN  Toggle stream COLOR: YUV"}},
    {AppView::Action::ToggleStreamCubeMap,           {"ToggleStreamCubeMap",             VK_UP,      "UP    Toggle stream CUBEMAP"}},
    {AppView::Action::NextExposureTime,              {"NextExposureTime",                '1',        "1     Camera exposure time"}},
    {AppView::Action::NextISOValue,                  {"NextISOValue",                    '2',        "2     Camera ISO value"}},
    {AppView::Action::NextWhiteBalance,              {"NextWhiteBalance",                '3',        "3     Camera white balance"}},
    {AppView::Action::NextFlickerCompensation,       {"NextFlickerCompensation",         '4',        "4     Camera anti flicker mode"}},
    {AppView::Action::NextSharpness,                 {"NextSharpness",                   '5',        "5     Camera sharpness mode"}},
#ifdef USE_EXPERIMENTAL_API
    {AppView::Action::NextEyeReproj,                 {"NextEyeReproj",                   '6',        "6     Camera eye reprojection mode"}},
#endif
    {AppView::Action::NextFocusDistance,             {"NextFocusDistance",               '7',        "7     Focus distance setting mode"}},
    {AppView::Action::SetVRViewOffset0,              {"SetVRViewOffset0",                'Q',        "Q     Set VR view offset 0%"}},
    {AppView::Action::SetVRViewOffset50,             {"SetVRViewOffset50",               'W',        "W     Set VR view offset 50%"}},
    {AppView::Action::SetVRViewOffset100,            {"SetVRViewOffset100",              'E',        "E     Set VR view offset 100%"}},
    {AppView::Action::ToggleRenderingVR,             {"ToggleRenderingVR",               'V',        "V     Toggle VR rendering"}},
    {AppView::Action::ToggleSubmittingVRDepth,       {"ToggleSubmittingVRDepth",         'D',        "D     Toggle VR depth submit"}},
    {AppView::Action::ToggleDepthTestRange,          {"ToggleDepthTestRange",            'Z',        "Z     Toggle VR depth test range: OFF, 3.0m, 1.5m, 0.5m, 0.0m"}},
    {AppView::Action::ResetCameraProperties,         {"ResetCameraProperties",           'R',        "R     Reset camera properties"}},
    {AppView::Action::ToggleReactConnectionEvents,   {"ToggleReactConnectionEvents",     'C',        "C     Toggle MR availability event handling"}},
    {AppView::Action::ToggleVRBackground,            {"ToggleVRBackground",              'B',        "B     Toggle VR background when no VST"}},
    {AppView::Action::ToggleVRColorCorrection,       {"ToggleVRColorCorrection",         'A',        "A     Toggle VR color correction to VST camera params"}},
    {AppView::Action::ToggleLighting,                {"ToggleLighting",                  'L',        "L     Toggle VR ambient lighting color gains: 6500K, 2800K"}},
    {AppView::Action::ToggleCubemapMode,             {"ToggleCubemapMode",               'K',        "K     Toggle environment cubemap mode: Fixed 6500K, Auto Adapt"}},
    {AppView::Action::DecreaseClientPriority,        {"DecreaseClientPriority",          'N',        "N     Decrease client priority"}},
    {AppView::Action::IncreaseClientPriority,        {"IncreaseClientPriority",          'M',        "M     Increase client priority"}},
    {AppView::Action::ToggleFrameRateLimiter,        {"ToggleFrameRateLimiter",          'F',        "F     Toggle frame rate limiter"}},
};
// clang-format on

// Window client area margin
constexpr int c_windowMargin = 8;

// Window client area size and log height
constexpr glm::ivec2 c_windowClientSize(800, 1024);
constexpr int c_logHeight = 308;

// VR view offset presets
// These values define interpolation factor between user eye position (0.0), and video-pass-through
// camera position. This is used to match VR view matrices to video-pass-through image.
static const std::vector<float> c_vrViewOffsets = {0.0f, 0.5f, 1.0f};

// VR depth test range presets (-1.0 = OFF = infinity)
// These values define the range (m) where depth testing is applied between video-pass-through image and VR surface.
static const std::vector<float> c_vrDepthTestRanges = {-1.0f, 3.0f, 1.5f, 0.5f, 0.0f};

// VR ambient lighting presets.
// This table contains RGB gain values used for VR scene lighting in different environment lighting temperatures (K).
// If selected preset temperature matches the real world lighting, the RGB gains should produce similar lighting for
// rendered VR scene. Lighting preset can be changed by ToggleLighting action ('L') and it is intended to work with
// the ToggleVRColorCorrection ('A'). The lighting values are then also adjusted to match the current camera settings.
static const std::vector<std::pair<int, glm::vec3>> c_ambientLightPresets = {
    {6500, {1.0f, 1.0f, 1.0f}},
    {2800, {2.13f, 0.81f, 0.108f}},
};

static const std::vector<const char*> c_ambientLightPresetNames = {"6500K", "2800K"};

static const std::vector<varjo_EnvironmentCubemapMode> c_cubemapModes = {varjo_EnvironmentCubemapMode_Fixed6500K, varjo_EnvironmentCubemapMode_AutoAdapt};
static const std::vector<const char*> c_cubemapModeNames = {"Fixed 6500K", "Auto Adapt"};

}  // namespace

namespace UIHelpers
{
void VSpace() { ImGui::Dummy(ImVec2(0.0f, 8.0f)); }

void HSpace()
{
    ImGui::Dummy(ImVec2(8.0f, 8.0f));
    ImGui::SameLine();
}

void separator()
{
    UIHelpers::VSpace();
    ImGui::Separator();
    UIHelpers::VSpace();
}

void pushDisabledIf(bool condition)
{
    if (condition) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
    }
}

void popDisabledIf(bool condition)
{
    if (condition) {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }
}

void setTooltip(const char* text)
{
    if (ImGui::IsItemHovered(0)) {
        ImGui::SetTooltip(text);
    }
}
}  // namespace UIHelpers

AppView::AppView(AppLogic& logic)
    : m_logic(logic)
{
    // Fill in key mappings
    for (const auto& ai : c_actions) {
        c_keyMappings[ai.second.keyCode] = ai.first;
    }

    // Present UI with vsync OFF (We sync to Varjo API instead).
    constexpr bool c_vsync = false;

    // Create user interface instance
    m_ui = std::make_unique<UI>(std::bind(&AppView::onFrame, this, std::placeholders::_1),    //
        std::bind(&AppView::onKeyPress, this, std::placeholders::_1, std::placeholders::_2),  //
        _T(APP_TITLE_TEXT), c_windowClientSize.x, c_windowClientSize.y, c_vsync);

    // Set log function
    LOG_INIT(std::bind(&UI::writeLogEntry, m_ui.get(), std::placeholders::_1, std::placeholders::_2), LogLevel::Info);

    LOG_INFO(APP_TITLE_TEXT);
    LOG_INFO(APP_COPYRIGHT_TEXT);
    LOG_INFO("-------------------------------");

    // Create contexts
    m_context = std::make_unique<GfxContext>(m_ui->getWindowHandle());

    // Additional ImgUi setup
    auto& io = ImGui::GetIO();

    // Disable storing UI ini file
    io.IniFilename = NULL;
}

AppView::~AppView()
{
    // Reset camera settings if we don't want to explicitly keep them after session
    if (m_uiState.resetCameraSettingsAtExit) {
        auto appState = m_logic.getState();
        LOG_INFO("Resetting camera properties at exit..");
        if (onAction(Action::ResetCameraProperties, appState)) {
            m_logic.setState(appState, false);
        }
    }

    // Deinit logger
    LOG_DEINIT();

    // Free UI
    m_ui.reset();
}

bool AppView::init()
{
    if (!m_logic.init(*m_context)) {
        LOG_ERROR("Initializing application failed.");
        return false;
    }

    // Reset states
    m_uiState = {};
    AppState appState = m_logic.getState();

    // Resolve UI indices
    resolveIndices(appState);

    // Force set initial state
    m_logic.setState(appState, true);

    return true;
}

void AppView::resolveIndices(const AppState& appState)
{
    // Resolve ambient light preset index
    {
        int i = 0;
        for (const auto& preset : c_ambientLightPresets) {
            if (preset.first == appState.options.ambientLightTempK) {
                break;
            }
            i++;
        }

        if (i >= 0 && i < static_cast<int>(c_ambientLightPresetNames.size())) {
            m_uiState.ambientLightIndex = i;
        }
    }
}

void AppView::run()
{
    LOG_DEBUG("Entering main loop.");

    // Run UI main loop
    m_ui->run();
}

bool AppView::onFrame(UI& ui)
{
    // Check UI instance
    if (&ui != m_ui.get()) {
        return false;
    }

    // Check for quit
    if (m_uiState.quitRequested) {
        LOG_INFO("Quit requested.");
        return false;
    }

#if DEBUG_FRAME_TIMING
    static std::chrono::nanoseconds maxDuration{0};
    static std::chrono::nanoseconds totDuration{0};
    static int frameCount = 0;
    const auto frameStartTime = std::chrono::high_resolution_clock::now();
#endif

    // Check for varjo events
    m_logic.checkEvents();

    // Update state to logic state
    updateUI();

    // Update application logic
    m_logic.update();

#if DEBUG_FRAME_TIMING
    // Frame timing
    const auto frameEndTime = std::chrono::high_resolution_clock::now();
    const auto frameDuration = frameEndTime - frameStartTime;
    totDuration += frameDuration;
    maxDuration = std::max(frameDuration, maxDuration);
    frameCount++;

    // Report frame timing
    static const std::chrono::seconds c_frameReportInterval(1);
    if (totDuration >= c_frameReportInterval) {
        LOG_INFO("Timing: frames=%d, fps=%f, avg=%f ms, max=%f ms, tot=%f ms", frameCount, 1e9f * frameCount / totDuration.count(),
            0.000001 * totDuration.count() / frameCount, 0.000001 * maxDuration.count(), 0.000001 * totDuration.count());
        maxDuration = {0};
        totDuration = {0};
        frameCount = 0;
    }
#endif

    // Return true to continue running
    return true;
}

bool AppView::onAction(Action actionType, AppState& appState)
{
    Action action = static_cast<Action>(actionType);
    if (c_actions.count(action) == 0) {
        LOG_ERROR("Unknown not found: %d", actionType);
        return false;
    }

    if (action != Action::None) {
        LOG_INFO("Action: %s", c_actions.at(action).name.c_str());
    }

    // App state dirty flag
    bool stateDirty = false;

    // Handle input actions
    switch (action) {
        case Action::None: {
            // Ignore
        } break;

        case Action::Quit: {
            m_uiState.quitRequested = true;
        } break;

        case Action::Help: {
            LOG_INFO("\nKeyboard Shortcuts:\n");
            std::multimap<int, AppView::Action> sortedShortcuts;
            for (const auto& ai : c_actions) {
                if (ai.second.keyCode > 0) {
                    sortedShortcuts.insert(std::make_pair(ai.second.keyCode, ai.first));
                }
            }
            for (const auto& it : sortedShortcuts) {
                if (it.second != Action::None) {
                    LOG_INFO("  %s", c_actions.at(it.second).help.c_str());
                }
            }
            LOG_INFO("");
        } break;

        case Action::Reset: {
            appState.options = {};
            stateDirty = true;
        } break;

        case Action::PrintCameraProperties: {
            m_logic.getCamera().printSupportedProperties();
        } break;

        case Action::PrintCurrentCameraConfig: {
            m_logic.getCamera().printCurrentPropertyConfig();
        } break;

        case Action::PrintStreamConfigs: {
            m_logic.getStreamer().printStreamConfigs();
        } break;

        case Action::ToggleRenderVideoOn: {
            appState.options.videoRenderingEnabled = true;
            stateDirty = true;
        } break;

        case Action::ToggleRenderVideoOff: {
            appState.options.videoRenderingEnabled = false;
            stateDirty = true;
        } break;

        case Action::ToggleVideoDepthEstimationOn: {
            appState.options.VideoDepthEstimationEnabled = true;
            stateDirty = true;
        } break;

        case Action::ToggleVideoDepthEstimationOff: {
            appState.options.VideoDepthEstimationEnabled = false;
            stateDirty = true;
        } break;

        case Action::ToggleVRViewOffset: {
            m_uiState.vrViewoffsetIndex = (m_uiState.vrViewoffsetIndex + 1) % c_vrViewOffsets.size();
            appState.options.vrViewOffset = c_vrViewOffsets[m_uiState.vrViewoffsetIndex];
            stateDirty = true;
        } break;

        case Action::ToggleBufferHandlingMode: {
            appState.options.delayedBufferHandlingEnabled = !appState.options.delayedBufferHandlingEnabled;
            stateDirty = true;
        } break;

        case Action::ToggleUndistortMode: {
            appState.options.undistortEnabled = !appState.options.undistortEnabled;
            stateDirty = true;
        } break;

        case Action::ToggleChromaKeying: {
            appState.options.chromaKeyingEnabled = !appState.options.chromaKeyingEnabled;
            stateDirty = true;
        } break;

        case Action::ToggleStreamColorYUV: {
            appState.options.dataStreamColorEnabled = !appState.options.dataStreamColorEnabled;
            stateDirty = true;
        } break;

        case Action::ToggleStreamCubeMap: {
            appState.options.dataStreamCubemapEnabled = !appState.options.dataStreamCubemapEnabled;
            stateDirty = true;
        } break;

        case Action::NextExposureTime: {
            m_logic.getCamera().applyNextModeOrValue(varjo_CameraPropertyType_ExposureTime);
        } break;

        case Action::NextISOValue: {
            m_logic.getCamera().applyNextModeOrValue(varjo_CameraPropertyType_ISOValue);
        } break;

        case Action::NextWhiteBalance: {
            m_logic.getCamera().applyNextModeOrValue(varjo_CameraPropertyType_WhiteBalance);
        } break;

        case Action::NextFlickerCompensation: {
            m_logic.getCamera().applyNextModeOrValue(varjo_CameraPropertyType_FlickerCompensation);
        } break;

        case Action::NextSharpness: {
            m_logic.getCamera().applyNextModeOrValue(varjo_CameraPropertyType_Sharpness);
        } break;

#ifdef USE_EXPERIMENTAL_API
        case Action::NextEyeReproj: {
            m_logic.getCamera().applyNextModeOrValue(varjo_CameraPropertyType_EyeReprojection);
        } break;
#endif
        case Action::NextFocusDistance: {
            m_logic.getCamera().applyNextModeOrValue(varjo_CameraPropertyType_FocusDistance);
        } break;
        case Action::SetVRViewOffset0:
        case Action::SetVRViewOffset50:
        case Action::SetVRViewOffset100: {
            m_uiState.vrViewoffsetIndex = static_cast<int>(action) - static_cast<int>(Action::SetVRViewOffset0);
            appState.options.vrViewOffset = c_vrViewOffsets[m_uiState.vrViewoffsetIndex];
            stateDirty = true;
        } break;

        case Action::ToggleRenderingVR: {
            appState.options.renderVREnabled = !appState.options.renderVREnabled;
            stateDirty = true;
        } break;

        case Action::ToggleSubmittingVRDepth: {
            appState.options.submitVRDepthEnabled = !appState.options.submitVRDepthEnabled;
            stateDirty = true;
        } break;

        case Action::ToggleDepthTestRange: {
            m_uiState.depthTestRangeIndex = (m_uiState.depthTestRangeIndex + 1) % c_vrDepthTestRanges.size();
            auto v = c_vrDepthTestRanges[m_uiState.depthTestRangeIndex];
            appState.options.vrDepthTestRangeEnabled = (v >= 0.0f);
            appState.options.vrDepthTestRangeValue = std::max(v, 0.0f);
            stateDirty = true;
        } break;

        case Action::ResetCameraProperties: {
            if (m_logic.isInitialized() && appState.general.mrAvailable) {
                m_logic.getCamera().resetPropertiesToDefaults();
                m_logic.getCamera().printCurrentPropertyConfig();
            }
        } break;

        case Action::ToggleReactConnectionEvents: {
            appState.options.reactToConnectionEvents = !appState.options.reactToConnectionEvents;
            stateDirty = true;
        } break;

        case Action::ToggleVRBackground: {
            appState.options.drawVRBackgroundEnabled = !appState.options.drawVRBackgroundEnabled;
            stateDirty = true;
        } break;

        case Action::ToggleVRColorCorrection: {
            appState.options.vrColorCorrectionEnabled = !appState.options.vrColorCorrectionEnabled;
            stateDirty = true;
        } break;

        case Action::ToggleLighting: {
            m_uiState.ambientLightIndex = (m_uiState.ambientLightIndex + 1) % c_ambientLightPresets.size();
            const auto& preset = c_ambientLightPresets[m_uiState.ambientLightIndex];
            appState.options.ambientLightTempK = preset.first;
            appState.options.ambientLightGainRGB = preset.second;
            stateDirty = true;
        } break;

        case Action::ToggleCubemapMode: {
            m_uiState.cubemapModeIndex = (m_uiState.cubemapModeIndex + 1) % c_cubemapModes.size();
            appState.options.cubemapMode = c_cubemapModes[m_uiState.cubemapModeIndex];
            stateDirty = true;
        } break;

        case Action::DecreaseClientPriority: {
            appState.options.clientPriority = appState.options.clientPriority - 1;
            stateDirty = true;
        } break;

        case Action::IncreaseClientPriority: {
            appState.options.clientPriority = appState.options.clientPriority + 1;
            stateDirty = true;
        } break;

        case Action::ToggleFrameRateLimiter: {
            appState.options.vrLimitFrameRate = !appState.options.vrLimitFrameRate;
            stateDirty = true;
        } break;

        default: {
            // Ignore unknown action
            LOG_ERROR("Unknown action: %d", action);
        } break;
    }

    return stateDirty;
}

void AppView::onKeyPress(UI& ui, int keyCode)
{
    if (m_uiState.anyItemActive) {
        // Ignore key handling if UI items active
        return;
    }

    // Check for input action
    Action action = Action::None;
    if (c_keyMappings.count(keyCode)) {
        action = c_keyMappings.at(keyCode);
    }

    // Get current state
    auto appState = m_logic.getState();

    // Handle action
    const bool stateDirty = onAction(action, appState);

    // Update state if changed
    if (stateDirty) {
        // Resolve UI indices
        resolveIndices(appState);

        // Set app logic state
        m_logic.setState(appState, false);
    }
}

void AppView::updateUI()
{
    // Update from logic state
    AppState appState = m_logic.getState();

    // VST Post Process window
    ImGui::Begin(APP_TITLE_TEXT);

    // Set initial size and pos
    {
        const float m = static_cast<float>(c_windowMargin);
        const float w = static_cast<float>(c_windowClientSize.x);
        const float h = static_cast<float>(c_windowClientSize.y - c_logHeight);
        ImGui::SetWindowPos(ImVec2(m, m), ImGuiCond_FirstUseEver);
        ImGui::SetWindowSize(ImVec2(w - 2 * m, h - 2 * m), ImGuiCond_FirstUseEver);
    }

#define _TAG "##appgeneric"

    {
        UIHelpers::VSpace();
        UIHelpers::HSpace();
        ImGui::BeginGroup();

        if (ImGui::Button("Reset" _TAG)) {
            onAction(Action::Reset, appState);
        }

        ImGui::SameLine();
        if (ImGui::Button("Help" _TAG)) {
            onAction(Action::Help, appState);
        }

        ImGui::SameLine();
        UIHelpers::HSpace();

        ImGui::PushItemWidth(120);
        ImGui::InputInt("Client order" _TAG, &appState.options.clientPriority);
        ImGui::PopItemWidth();

        ImGui::EndGroup();
    }
#undef _TAG

    UIHelpers::separator();

#define _TAG "##mixedreality"
    {
        {
            ImGui::Text("Mixed Reality:");
            UIHelpers::VSpace();
            UIHelpers::HSpace();
            ImGui::BeginGroup();

            {
                ImGui::Checkbox("Video rendering" _TAG, &appState.options.videoRenderingEnabled);
                ImGui::SameLine();
                ImGui::Checkbox("React MR events" _TAG, &appState.options.reactToConnectionEvents);
                ImGui::SameLine();
                ImGui::Checkbox("Chroma keying" _TAG, &appState.options.chromaKeyingEnabled);
            }

            {
                ImGui::Checkbox("Video depth test" _TAG, &appState.options.VideoDepthEstimationEnabled);

                UIHelpers::pushDisabledIf(!appState.options.VideoDepthEstimationEnabled);
                ImGui::SameLine();
                ImGui::Checkbox("Depth test range" _TAG, &appState.options.vrDepthTestRangeEnabled);

                {
                    UIHelpers::pushDisabledIf(!appState.options.vrDepthTestRangeEnabled);
                    ImGui::SameLine();
                    ImGui::PushItemWidth(120);
                    ImGui::SliderFloat("##Depth range value" _TAG, &appState.options.vrDepthTestRangeValue, 0.0f, 5.0f, "%.2f");
                    appState.options.vrDepthTestRangeValue = std::max(appState.options.vrDepthTestRangeValue, 0.0f);
                    ImGui::PopItemWidth();
                    UIHelpers::popDisabledIf(!appState.options.vrDepthTestRangeEnabled);
                }

                UIHelpers::popDisabledIf(!appState.options.VideoDepthEstimationEnabled);

                ImGui::SameLine();
                UIHelpers::HSpace();

                {
                    ImGui::PushItemWidth(120);
                    ImGui::SliderFloat("View offset" _TAG, &appState.options.vrViewOffset, 0.0f, 1.0f, "%.2f");
                    ImGui::PopItemWidth();
                }
            }

            ImGui::EndGroup();
        }
    }
#undef _TAG

    UIHelpers::separator();

#define _TAG "##scene"
    {
        ImGui::Text("Virtual Scene:");
        UIHelpers::VSpace();
        UIHelpers::HSpace();
        ImGui::BeginGroup();

        {
            ImGui::Checkbox("Render scene" _TAG, &appState.options.renderVREnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Submit depth" _TAG, &appState.options.submitVRDepthEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Render background" _TAG, &appState.options.drawVRBackgroundEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("Limit framerate (45Hz)" _TAG, &appState.options.vrLimitFrameRate);
        }

        ImGui::EndGroup();
    }
#undef _TAG

    UIHelpers::separator();

    ImGui::Columns(2);

#define _TAG "##camera"
    {
        ImGui::Text("Camera Settings:");

        UIHelpers::VSpace();
        UIHelpers::HSpace();
        ImGui::BeginGroup();
        updateCameraPropertiesUI();
        UIHelpers::VSpace();

        if (ImGui::Button("Reset" _TAG)) {
            onAction(Action::ResetCameraProperties, appState);
        }

        ImGui::SameLine();
        ImGui::Checkbox("Reset at Exit" _TAG, &m_uiState.resetCameraSettingsAtExit);
        ImGui::EndGroup();
    }
#undef _TAG

    ImGui::NextColumn();

#define _TAG "##cubemap"
    {
        ImGui::Text("Cubemap settings:");

        UIHelpers::VSpace();
        UIHelpers::HSpace();
        ImGui::BeginGroup();

        {
            ImGui::Text("Mode:");
            ImGui::SameLine();

            ImGui::PushItemWidth(120);
            ImGui::Combo("##Cubemap mode" _TAG, &m_uiState.cubemapModeIndex, c_cubemapModeNames.data(), static_cast<int>(c_cubemapModeNames.size()));
            appState.options.cubemapMode = c_cubemapModes[m_uiState.cubemapModeIndex];
            ImGui::PopItemWidth();
        }

        ImGui::EndGroup();
        UIHelpers::VSpace();

        ImGui::Text("Color adaptation:");

        UIHelpers::VSpace();
        UIHelpers::HSpace();
        ImGui::BeginGroup();

        {
            const bool disableColorCorrection =
                appState.options.dataStreamCubemapEnabled && appState.options.cubemapMode == varjo_EnvironmentCubemapMode_AutoAdapt;
            UIHelpers::pushDisabledIf(disableColorCorrection);

            ImGui::Checkbox("Adapt colors" _TAG, &appState.options.vrColorCorrectionEnabled);

            ImGui::SameLine();
            UIHelpers::HSpace();

            ImGui::Text("Ambient color:");
            ImGui::SameLine();

            ImGui::PushItemWidth(70);
            ImGui::Combo("##Ambient light preset" _TAG, &m_uiState.ambientLightIndex, c_ambientLightPresetNames.data(),
                static_cast<int>(c_ambientLightPresetNames.size()));
            appState.options.ambientLightTempK = c_ambientLightPresets[m_uiState.ambientLightIndex].first;
            appState.options.ambientLightGainRGB = c_ambientLightPresets[m_uiState.ambientLightIndex].second;
            ImGui::PopItemWidth();

            UIHelpers::popDisabledIf(disableColorCorrection);
        }

        ImGui::EndGroup();
    }
#undef _TAG

    ImGui::Columns(1);

    UIHelpers::separator();

#define _TAG "##datastreams"
    {
        ImGui::Text("Data Streaming:");
        UIHelpers::VSpace();
        UIHelpers::HSpace();
        ImGui::BeginGroup();

        if (ImGui::Button("Print Configs" _TAG)) {
            onAction(Action::PrintStreamConfigs, appState);
        }

        ImGui::SameLine();
        UIHelpers::HSpace();
        ImGui::Checkbox("Stream: Color" _TAG, &appState.options.dataStreamColorEnabled);
        ImGui::SameLine();
        ImGui::Checkbox("Stream: Cubemap" _TAG, &appState.options.dataStreamCubemapEnabled);

        ImGui::SameLine();
        UIHelpers::HSpace();
        ImGui::Checkbox("Delayed handling" _TAG, &appState.options.delayedBufferHandlingEnabled);
        ImGui::SameLine();
        ImGui::Checkbox("Undistort color stream" _TAG, &appState.options.undistortEnabled);

        UIHelpers::VSpace();
        ImGui::Text("Status: %s", m_logic.getStreamer().getStatusLine().c_str());

        ImGui::EndGroup();
    }
#undef _TAG

    UIHelpers::separator();

    {
        if (!appState.general.mrAvailable) {
            const auto colTxt = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(colTxt.x, colTxt.y, colTxt.z * 0.2f, colTxt.w));
        }
        ImGui::Text(appState.general.mrAvailable ? "Status: Mixed reality features available." : "Warning: Mixed Reality features not available.");
        if (!appState.general.mrAvailable) {
            ImGui::PopStyleColor(1);
        }

        ImGui::Text("Frame timing: %.3f fps / %.3f ms / %.3f s / %d frames",  //
            ImGui::GetIO().Framerate,                                         //
            1000.0f / ImGui::GetIO().Framerate,                               //
            appState.general.frameTime, appState.general.frameCount);
    }

    // End main window
    ImGui::End();

    // Log window
    {
        ImGui::Begin("Log");

        // Set initial size and pos
        {
            const float m = static_cast<float>(c_windowMargin);
            const float w = static_cast<float>(c_windowClientSize.x);
            const float h0 = static_cast<float>(c_windowClientSize.y - c_logHeight);
            const float h1 = static_cast<float>(c_logHeight);
            ImGui::SetWindowPos(ImVec2(m, h0), ImGuiCond_FirstUseEver);
            ImGui::SetWindowSize(ImVec2(w - 2 * m, h1 - m), ImGuiCond_FirstUseEver);
        }

        m_ui->drawLog();
        ImGui::End();
    }

    // --- UI helper definitions --->
#undef _TAG
    // <--- UI helper definitions ---

    // Set UI item active flag
    m_uiState.anyItemActive = ImGui::IsAnyItemActive();

    // Update state from UI back to logic
    m_logic.setState(appState, false);
}

void AppView::updateCameraPropertiesUI()
{
    const auto& types = m_logic.getCamera().getPropertyTypes();

    for (auto& type : types) {
        // For tagging the controls
        const std::string propertyTag = "##" + CameraManager::propertyTypeToString(type, true);

        ImGui::Text((CameraManager::propertyTypeToString(type) + ":").c_str());
        const CameraPropertyInfo& propInfo = m_logic.getCamera().getPropertyInfo(type);

        if (propInfo.supportedModes.empty()) {
            ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
            ImGui::Text("Unavailable");
            ImGui::PopStyleVar();
        } else {
            // Create buttons for each supported mode
            for (auto mode : propInfo.supportedModes) {
                ImGui::SameLine();
                bool isActive = (mode == propInfo.curMode);

                if (isActive) {
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                } else {
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
                }

                if (ImGui::Button((CameraManager::propertyModeToString(mode) + propertyTag).c_str())) {
                    m_logic.getCamera().setMode(type, mode);
                }

                if (isActive) {
                    ImGui::PopItemFlag();
                } else {
                    ImGui::PopStyleVar();
                }
            }

            // Create a control for adjusting the manual value
            if (propInfo.curMode == varjo_CameraPropertyMode_Manual) {
                ImGui::SameLine();

                if (propInfo.configType == varjo_CameraPropertyConfigType_Range) {
                    // Create a slider for a continuous range parameter
                    ImGui::PushItemWidth(90);

                    switch (propInfo.curValue.type) {
                        case varjo_CameraPropertyDataType_Int: {
                            int origValue = static_cast<int>(propInfo.curValue.value.intValue);
                            int value = origValue;

                            ImGui::InputInt(propertyTag.c_str(), &value, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue);
                            const int minVal = static_cast<int>(propInfo.supportedValues[0].value.intValue);
                            const int maxVal = static_cast<int>(propInfo.supportedValues[1].value.intValue);
                            value = std::clamp(value, minVal, maxVal);
                            std::ostringstream ss;
                            ss << "Min value: " << minVal << std::endl << "Max value: " << maxVal;
                            UIHelpers::setTooltip(ss.str().c_str());

                            // Create camera command if the value was changed by the user
                            if (value != origValue) {
                                varjo_CameraPropertyValue propertyValue = {};
                                propertyValue.type = propInfo.curValue.type;
                                propertyValue.value.intValue = static_cast<int64_t>(value);
                                m_logic.getCamera().setValue(type, propertyValue);
                            }
                        } break;

                        case varjo_CameraPropertyDataType_Double: {
                            double origValue = propInfo.curValue.value.doubleValue;
                            double value = origValue;

                            ImGui::InputDouble(propertyTag.c_str(), &value, 0.0, 0.0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue);
                            const double minVal = propInfo.supportedValues[0].value.doubleValue;
                            const double maxVal = propInfo.supportedValues[1].value.doubleValue;
                            value = std::clamp(value, minVal, maxVal);
                            std::ostringstream ss;
                            ss << "Min value: " << std::fixed << std::setprecision(3) << minVal << std::endl << "Max value: " << maxVal;
                            UIHelpers::setTooltip(ss.str().c_str());

                            // Create camera command if the value was changed by the user
                            if (value != origValue) {
                                varjo_CameraPropertyValue propertyValue = {};
                                propertyValue.type = propInfo.curValue.type;
                                propertyValue.value.doubleValue = static_cast<double>(value);
                                m_logic.getCamera().setValue(type, propertyValue);
                            }
                        } break;
                    }

                    ImGui::PopItemWidth();
                } else if (propInfo.configType == varjo_CameraPropertyConfigType_List) {
                    // Calculate optimal width for the combo box based on the items.
                    float maxItemWidth = 0.0f;
                    for (const auto& value : propInfo.supportedValues) {
                        std::string valueStr = CameraManager::propertyValueToString(type, value);
                        maxItemWidth = std::max(maxItemWidth, ImGui::CalcTextSize(valueStr.c_str()).x);
                    }

                    constexpr float c_minComboBoxWidth = 80.0f;
                    constexpr float c_comboBoxMargin = 30.0f;  // To take into account the drop down button area.
                    float comboBoxWidth = std::max(c_minComboBoxWidth, maxItemWidth + c_comboBoxMargin);
                    ImGui::PushItemWidth(comboBoxWidth);

                    // Create a combo box for a discrete list of manual values
                    std::string curValueStr = CameraManager::propertyValueToString(type, propInfo.curValue);
                    if (ImGui::BeginCombo(propertyTag.c_str(), curValueStr.c_str())) {
                        for (const auto& value : propInfo.supportedValues) {
                            std::string valueStr = CameraManager::propertyValueToString(type, value);
                            bool isSelected = (curValueStr == valueStr);
                            if (ImGui::Selectable(valueStr.c_str(), isSelected)) {
                                m_logic.getCamera().setValue(type, value);
                            }

                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }

                        ImGui::EndCombo();
                    }

                    ImGui::PopItemWidth();
                }
            }
        }
    }
}

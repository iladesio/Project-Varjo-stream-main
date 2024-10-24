// Copyright 2019-2021 Varjo Technologies Oy. All rights reserved.

#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <imgui.h>

#include "Globals.hpp"
#include "UI.hpp"

#include "GfxContext.hpp"
#include "AppLogic.hpp"

//! Application view class
class AppView
{
public:
    // Input actions
    enum class Action {
        None = 0,
        Quit,
        Help,
        Reset,
        PrintCameraProperties,
        PrintCurrentCameraConfig,
        PrintStreamConfigs,
        ToggleRenderVideoOn,
        ToggleRenderVideoOff,
        ToggleVideoDepthEstimationOn,
        ToggleVideoDepthEstimationOff,
        ToggleStreamColorYUV,
        ToggleStreamCubeMap,
        ToggleVRViewOffset,
        SetVRViewOffset0,
        SetVRViewOffset50,
        SetVRViewOffset100,
        NextExposureTime,
        NextISOValue,
        NextWhiteBalance,
        NextFlickerCompensation,
        NextSharpness,
#ifdef USE_EXPERIMENTAL_API
        NextEyeReproj,
#endif
        NextFocusDistance,
        ToggleBufferHandlingMode,
        ToggleUndistortMode,
        ToggleRenderingVR,
        ToggleSubmittingVRDepth,
        ToggleDepthTestRange,
        ResetCameraProperties,
        ToggleReactConnectionEvents,
        ToggleVRBackground,
        ToggleVRColorCorrection,
        ToggleLighting,
        ToggleChromaKeying,
        ToggleCubemapMode,
        DecreaseClientPriority,
        IncreaseClientPriority,
        ToggleFrameRateLimiter,
    };

    //! UI specific state
    struct UIState {
        bool anyItemActive{false};             //!< True if any UI item active. Ignore keys then.
        bool quitRequested{false};             //!< Quit requested
        bool resetCameraSettingsAtExit{true};  //!< Don't reset camera on application exit
        int depthTestRangeIndex{0};            //!< Depth test range limitation
        int vrViewoffsetIndex{-1};             //!< Current VR view offset index
        int ambientLightIndex{0};              //!< Current ambient lighting index
        int cubemapModeIndex{0};               //!< Current cubemap mode index
    };

public:
    //! Constructor
    AppView(AppLogic& logic);

    //! Destructor
    ~AppView();

    // Disable copy and assign
    AppView(const AppView& other) = delete;
    AppView(const AppView&& other) = delete;
    AppView& operator=(const AppView& other) = delete;
    AppView& operator=(const AppView&& other) = delete;

    //! Initialize application
    bool init();

    //! Application main loop
    void run();

private:
    //! UI frame callback
    bool onFrame(VarjoExamples::UI& ui);

    //! UI key press callback
    void onKeyPress(VarjoExamples::UI& ui, int keyCode);

    //! Updates UI based on logic state and writes changes back to it.
    void updateUI();

    //! Handle UI action. Return true if state changed and should be handled.
    bool onAction(Action actionType, AppState& appState);

    //! Resolve UI indices
    void resolveIndices(const AppState& appState);

private:
    //! Updates camera properties UI and applies any changes to the camera properties
    void updateCameraPropertiesUI();

    AppLogic& m_logic;                                     //!< App logic instance
    std::unique_ptr<VarjoExamples::UI> m_ui;               //!< User interface wrapper
    std::unique_ptr<VarjoExamples::GfxContext> m_context;  //!< Graphics contexts
    UIState m_uiState{};                                   //!< UI specific states
};

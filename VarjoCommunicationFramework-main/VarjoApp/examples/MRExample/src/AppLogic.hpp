// Copyright 2019-2021 Varjo Technologies Oy. All rights reserved.

#pragma once

#include <memory>
#include <chrono>
#include <glm/glm.hpp>

#include "Globals.hpp"
#include "D3D11Renderer.hpp"
#include "MultiLayerView.hpp"
#include "Scene.hpp"
#include "MarkerTracker.hpp"
#include "CameraManager.hpp"
#include "DataStreamer.hpp"

#include "AppState.hpp"
#include "GfxContext.hpp"
#include "MRScene.hpp"

//! Application logic class
class AppLogic
{
public:
    //! Constructor
    AppLogic() = default;

    //! Destruictor
    ~AppLogic();

    // Disable copy and assign
    AppLogic(const AppLogic& other) = delete;
    AppLogic(const AppLogic&& other) = delete;
    AppLogic& operator=(const AppLogic& other) = delete;
    AppLogic& operator=(const AppLogic&& other) = delete;

    //! Initialize application
    bool init(VarjoExamples::GfxContext& context);

    //! Check for Varjo API events
    void checkEvents();

    //! Update application state
    void setState(const AppState& appState, bool force);

    //! Returns application state
    const AppState& getState() const { return m_appState; }

    //! Update application
    void update();

    //! Return true is app has been initialized successfully
    bool isInitialized() const { return m_initialized; }

    //! Return camera manager instance
    VarjoExamples::CameraManager& getCamera() const { return *m_camera; }

    //! Return data streamer instance
    VarjoExamples::DataStreamer& getStreamer() const { return *m_streamer; }

private:
    //! Toggle VST rendering
    void setVideoRendering(bool enabled);

    //! Handle mixed reality availablity
    void onMixedRealityAvailable(bool available, bool forceSetState);

    //! Handles new color stream frames from the data streamer (run in DataStreamer's worker thread)
    void onFrameReceived(const VarjoExamples::DataStreamer::Frame& frame);

private:
    bool m_initialized{false};                                   //!< App initialized flag
    varjo_Session* m_session{nullptr};                           //!< Varjo session
    std::unique_ptr<VarjoExamples::D3D11Renderer> m_renderer;    //!< Renderer instance
    std::unique_ptr<VarjoExamples::MultiLayerView> m_varjoView;  //!< Varjo layer ext view instance
    AppState m_appState{};                                       //!< Application state

    std::unique_ptr<MRScene> m_scene;                         //!< Scene instance
    std::unique_ptr<VarjoExamples::DataStreamer> m_streamer;  //!< Data streamer instance
    std::unique_ptr<VarjoExamples::CameraManager> m_camera;   //!< Camera manager instance

    struct FrameData {
        std::optional<varjo_DistortedColorFrameMetadata> metadata;                     //!< Color stream metadata
        std::array<std::optional<VarjoExamples::DataStreamer::Frame>, 2> colorFrames;  //!< Color stream stereo frames
        std::optional<VarjoExamples::DataStreamer::Frame> cubemapFrame;                //!< HDR cubemap frame
        std::optional<varjo_EnvironmentCubemapFrameMetadata> cubemapMetadata;          //!< HDR cubemap metadata
    };
    FrameData m_frameData;        //!< Latest frame data
    std::mutex m_frameDataMutex;  //!< Mutex for locking frame data

    varjo_TextureFormat m_colorStreamFormat{varjo_TextureFormat_INVALID};  //!< Texture format for color stream

    SOCKET sock;
};

// Copyright 2019-2021 Varjo Technologies Oy. All rights reserved.

#pragma once

#include <glm/glm.hpp>

#include "Globals.hpp"
#include "MarkerTracker.hpp"

//! Application state struct
struct AppState {
    //! General params structure
    struct General {
        double frameTime{0.0f};   //!< Current frame time
        int64_t frameCount{0};    //!< Current frame count
        bool mrAvailable{false};  //!< Mixed reality available flag
    };

    //! Options structure
    struct Options {
        int clientPriority{0};                            //!< Client layer priority
        bool videoRenderingEnabled{true};                 //!< Render VST image flag
        bool VideoDepthEstimationEnabled{false};          //!< VST depth estimation flag
        bool renderVREnabled{true};                       //!< VR rendering enabled flag
        bool submitVRDepthEnabled{true};                  //!< VR layer submit enabled flag
        bool drawVRBackgroundEnabled{true};               //!< VR background enabled flag
        bool vrColorCorrectionEnabled{false};             //!< VR color correction enabled flag
        bool chromaKeyingEnabled{false};                  //!< Chroma keying flag
        bool reactToConnectionEvents{true};               //!< Flag for reacting to MR connect/disconnect events
        bool dataStreamColorEnabled{false};               //!< Color data stream enabled flag
        bool dataStreamCubemapEnabled{false};             //!< Cubemap data stream enabled flag
        bool delayedBufferHandlingEnabled{false};         //!< Delayed data stream buffer handling
        bool undistortEnabled{false};                     //!< Undistort color datastream when saving to file
        float vrViewOffset{1.0};                          //!< VR view offset value
        bool vrDepthTestRangeEnabled{false};              //!< VR depth test range enabled flag
        float vrDepthTestRangeValue{1.0f};                //!< VR depth test range value
        int ambientLightTempK{6500};                      //!< Ambient light color temperature
        glm::vec3 ambientLightGainRGB{1.0f, 1.0f, 1.0f};  //!< Ambient light RGB color gains
        varjo_EnvironmentCubemapMode cubemapMode;         //!< Cubemap mode
        bool vrLimitFrameRate{false};                     //!< Force frame rate to lower
    };

    General general{};  //!< General params
    Options options{};  //!< Current state
};

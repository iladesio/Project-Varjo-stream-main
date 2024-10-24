// Copyright 2019-2021 Varjo Technologies Oy. All rights reserved.

#include <winsock2.h>
#include <WS2tcpip.h>
#include <stdio.h>
#include <windows.h>

#include "AppLogic.hpp"

#include <cstdio>
#include <vector>
#include <string>
#include <fstream>
#include <set>
#include <thread>

#include <Varjo.h>
#include <Varjo_events.h>
#include <Varjo_mr.h>
#include <Varjo_d3d11.h>
#include <glm/gtx/matrix_decompose.hpp>

#include "D3D11MultiLayerView.hpp"


#include <iostream>
#include <opencv2/opencv.hpp>  // Ensure you have OpenCV for image processing

#pragma comment(lib, "Ws2_32.lib")



// VarjoExamples namespace contains simple example wrappers for using Varjo API features.
// These are only meant to be used in SDK example applications. In your own application,
// use your own production quality integration layer.
using namespace VarjoExamples;

//---------------------------------------------------------------------------

AppLogic::~AppLogic()
{
    // Free camera manager resources
    m_camera.reset();

    // Free data stremer resources
    m_streamer.reset();

    // Free scene resources
    m_scene.reset();

    // Free view resources
    m_varjoView.reset();

    // Free renderer resources
    m_renderer.reset();

    // Shutdown the varjo session. Can't check errors anymore after this.
    LOG_DEBUG("Shutting down Varjo session..");
    varjo_SessionShutDown(m_session);
    m_session = nullptr;

    // Close the socket if it's valid
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        WSACleanup();
    }
}

void initializeWinsock()
{
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        exit(EXIT_FAILURE);
    }
}

SOCKET createAndConnectSocket(const std::string& serverIP, int serverPort)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverIP.c_str(), &serverAddress.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "Socket connection failed: " << WSAGetLastError() << std::endl;
        closesocket(sock);
        WSACleanup();
        exit(EXIT_FAILURE);
    }

    return sock;
}

void sendImageToServer(SOCKET sock, const std::vector<uint8_t>& bufferRGBA, int width, int height)
{
    cv::Mat img(height, width, CV_8UC4, const_cast<uint8_t*>(bufferRGBA.data()));
    std::vector<uchar> encodedImg;
    cv::imencode(".png", img, encodedImg);

    uint64_t dataSize = encodedImg.size();

    // Send the size of the encoded image first
    int bytesSent = send(sock, reinterpret_cast<const char*>(&dataSize), sizeof(dataSize), 0);
    std::cout << "Sent image size (" << sizeof(dataSize) << " bytes): " << bytesSent << " bytes sent." << std::endl;

    // Wait for ACK_SIZE from the server
    char ackSze[3] = { 0 };
    int bytesReceived = recv(sock, ackSze, 3, 0);
    if (bytesReceived <= 0 || std::string(ackSze, 3) != "SZE") {
        std::cerr << "Failed to receive size acknowledgment from server. " << std::string(ackSze, 3) << std::endl;
        return;
    }

    // Send the actual image data
    bytesSent = send(sock, reinterpret_cast<const char*>(encodedImg.data()), encodedImg.size(), 0);

    // Wait for ACK_IMAGE from the server
    char ackImg[3] = { 0 };
    bytesReceived = recv(sock, ackImg, 3, 0);
    if (bytesReceived <= 0 || std::string(ackImg, 3) != "IMG") {
        std::cerr << "Failed to receive image acknowledgment from server. " << std::string(ackImg, 3) << std::endl;
    }
}





bool AppLogic::init(GfxContext& context)
{
    initializeWinsock();
    sock = createAndConnectSocket("192.168.137.38", 9999);  // Replace with your server IP and port

    // Initialize the varjo session.
    LOG_DEBUG("Initializing Varjo session..");
    m_session = varjo_SessionInit();
    if (CHECK_VARJO_ERR(m_session) != varjo_NoError) {
        LOG_ERROR("Creating Varjo session failed.");
        return false;
    }

    {
        // Get graphics adapter used by Varjo session
        auto dxgiAdapter = D3D11MultiLayerView::getAdapter(m_session);

        // Init graphics
        context.init(dxgiAdapter.Get());

        // Create D3D11 renderer instance.
        auto d3d11Renderer = std::make_unique<D3D11Renderer>(dxgiAdapter.Get());

        // Create varjo multi layer view
        m_varjoView = std::make_unique<D3D11MultiLayerView>(m_session, *d3d11Renderer);

        // Store as generic renderer
        m_renderer = std::move(d3d11Renderer);
    }

    // Create scene instance
    m_scene = std::make_unique<MRScene>(*m_renderer);

    // Create data streamer instance
    m_streamer = std::make_unique<DataStreamer>(m_session, std::bind(&AppLogic::onFrameReceived, this, std::placeholders::_1));

    // Create mixed reality camera manager instance
    m_camera = std::make_unique<CameraManager>(m_session);

    // Check if Mixed Reality features are available.
    varjo_Bool mixedRealityAvailable = varjo_False;
    varjo_SyncProperties(m_session);
    CHECK_VARJO_ERR(m_session);
    if (varjo_HasProperty(m_session, varjo_PropertyKey_MRAvailable)) {
        mixedRealityAvailable = varjo_GetPropertyBool(m_session, varjo_PropertyKey_MRAvailable);
    }

    // Handle mixed reality availability
    onMixedRealityAvailable(mixedRealityAvailable == varjo_True, false);

    if (mixedRealityAvailable == varjo_True) {
        LOG_INFO("Varjo Mixed Reality features available!");

        // Optionally reset camera at startup
        constexpr bool resetCameraAtStart = true;
        if (resetCameraAtStart) {
            // Reset camera properties to defaults
            m_camera->resetPropertiesToDefaults();

            // Set camera exposure and white balance to auto
            m_camera->setAutoMode(varjo_CameraPropertyType_ExposureTime);
            m_camera->setAutoMode(varjo_CameraPropertyType_WhiteBalance);
        }

    } else {
        LOG_WARNING("Varjo Mixed Reality features not available!");
    }

    m_initialized = true;

    return true;
}

void AppLogic::setVideoRendering(bool enabled)
{
    varjo_MRSetVideoRender(m_session, enabled ? varjo_True : varjo_False);
    if (CHECK_VARJO_ERR(m_session) == varjo_NoError) {
        LOG_INFO("Video rendering: %s", enabled ? "ON" : "OFF");
    }
    m_appState.options.videoRenderingEnabled = enabled;
}

void AppLogic::setState(const AppState& appState, bool force)
{
    // Store previous state and set new one
    const auto prevState = m_appState;
    m_appState = appState;

    // Client priority
    if (force || appState.options.clientPriority != prevState.options.clientPriority) {
        varjo_SessionSetPriority(m_session, appState.options.clientPriority);
        if (CHECK_VARJO_ERR(m_session) == varjo_NoError) {
            LOG_INFO("Client priority: %i", appState.options.clientPriority);
        }
    }
    // Check for mixed reality availability
    if (!m_appState.general.mrAvailable) {
        // Toggle video rendering off
        if (m_appState.options.videoRenderingEnabled) {
            setVideoRendering(false);
        }
        return;
    }

    // Video rendering
    if (force || appState.options.videoRenderingEnabled != prevState.options.videoRenderingEnabled) {
        setVideoRendering(appState.options.videoRenderingEnabled);
    }

    // Video depth estimation
    if (force || appState.options.VideoDepthEstimationEnabled != prevState.options.VideoDepthEstimationEnabled) {
        varjo_MRSetVideoDepthEstimation(m_session, appState.options.VideoDepthEstimationEnabled ? varjo_True : varjo_False);
        if (CHECK_VARJO_ERR(m_session) == varjo_NoError) {
            LOG_INFO("Video depth estimation: %s", appState.options.VideoDepthEstimationEnabled ? "ON" : "OFF");
        }
    }

    // Chroma keying
    if (force || appState.options.chromaKeyingEnabled != prevState.options.chromaKeyingEnabled) {
        varjo_MRSetChromaKey(m_session, appState.options.chromaKeyingEnabled ? varjo_True : varjo_False);
        if (CHECK_VARJO_ERR(m_session) == varjo_NoError) {
            LOG_INFO("Chroma keying: %s", appState.options.chromaKeyingEnabled ? "ON" : "OFF");
        }
    }

    // VR view offset
    if (force || appState.options.vrViewOffset != prevState.options.vrViewOffset) {
        varjo_MRSetVRViewOffset(m_session, appState.options.vrViewOffset);
        if (CHECK_VARJO_ERR(m_session) == varjo_NoError) {
            LOG_INFO("VR view offset: %.1f", appState.options.vrViewOffset);
        }
    }

    // VR rendering
    if (force || appState.options.renderVREnabled != prevState.options.renderVREnabled) {
        LOG_INFO("Rendering VR layer: %s", appState.options.renderVREnabled ? "ON" : "OFF");
    }

    // VR depth submit
    if (force || appState.options.submitVRDepthEnabled != prevState.options.submitVRDepthEnabled) {
        LOG_INFO("Submitting VR depth: %s", appState.options.submitVRDepthEnabled ? "ON" : "OFF");
    }

    // VR depth test range
    if (force || (appState.options.vrDepthTestRangeEnabled != prevState.options.vrDepthTestRangeEnabled) ||
        (appState.options.vrDepthTestRangeValue != prevState.options.vrDepthTestRangeValue)) {
        LOG_INFO("Depth test range: %s %.2f", (appState.options.vrDepthTestRangeEnabled ? "ON" : "OFF"), appState.options.vrDepthTestRangeValue);
    }

    // React MR events
    if (force || appState.options.reactToConnectionEvents != prevState.options.reactToConnectionEvents) {
        LOG_INFO("Handling connection events: %s", appState.options.reactToConnectionEvents ? "ON" : "OFF");
    }

    // VR background
    if (force || appState.options.drawVRBackgroundEnabled != prevState.options.drawVRBackgroundEnabled) {
        std::string warn = (appState.options.videoRenderingEnabled && appState.options.drawVRBackgroundEnabled) ? " (not visible with VST)" : "";
        LOG_INFO("Drawing VR background: %s %s", appState.options.drawVRBackgroundEnabled ? "ON" : "OFF", warn.c_str());
    }

    // VR ambient light
    if (force || appState.options.ambientLightTempK != prevState.options.ambientLightTempK ||
        appState.options.ambientLightGainRGB != prevState.options.ambientLightGainRGB) {
        LOG_INFO("VR ambient light (%dK): [%f, %f, %f]", appState.options.ambientLightTempK, appState.options.ambientLightGainRGB[0],
            appState.options.ambientLightGainRGB[1], appState.options.ambientLightGainRGB[2]);
    }

    // VR color correction
    if (force || appState.options.vrColorCorrectionEnabled != prevState.options.vrColorCorrectionEnabled) {
        LOG_INFO("Color correcting VR: %s", appState.options.vrColorCorrectionEnabled ? "ON" : "OFF");

        const auto reqType = varjo_StreamType_DistortedColor;
        const auto reqFormat = m_colorStreamFormat;
        const auto reqChannels = varjo_ChannelFlag_None;

        varjo_ChannelFlag runningChannels = varjo_ChannelFlag_None;
        if (appState.options.vrColorCorrectionEnabled) {
            if (m_streamer->isStreaming(reqType, reqFormat, runningChannels)) {
                LOG_INFO("Already streaming color stream..");
            } else {
                LOG_INFO("Starting metadata only color stream..");
                m_streamer->startDataStream(reqType, reqFormat, reqChannels);
            }
        } else {
            if (m_streamer->isStreaming(reqType, reqFormat, runningChannels)) {
                if (runningChannels == reqChannels) {
                    LOG_INFO("Stop metadata only color stream..");
                    m_streamer->stopDataStream(reqType, reqFormat);
                } else {
                    LOG_INFO("Keep streaming color with data..");
                }
            } else {
                // Not streaming, nothing to do.
            }
        }
    }

    // Cubemap config
    if (force || appState.options.cubemapMode != prevState.options.cubemapMode) {
        auto ret = varjo_Lock(m_session, varjo_LockType_EnvironmentCubemap);
        CHECK_VARJO_ERR(m_session);
        if (ret == varjo_True) {
            varjo_EnvironmentCubemapConfig cubemapConfig;
            cubemapConfig.mode = appState.options.cubemapMode;
            varjo_MRSetEnvironmentCubemapConfig(m_session, &cubemapConfig);

            if (CHECK_VARJO_ERR(m_session) == varjo_NoError) {
                LOG_INFO("Cubemap mode: %s", (appState.options.cubemapMode == varjo_EnvironmentCubemapMode_AutoAdapt) ? "Auto Adapt" : "Fixed 6500K");
            }

            varjo_Unlock(m_session, varjo_LockType_EnvironmentCubemap);
            CHECK_VARJO_ERR(m_session);
        } else {
            LOG_ERROR("Could not change cubemap config.");
        }
    }

    // Data stream buffer handling
    if (force || appState.options.delayedBufferHandlingEnabled != prevState.options.delayedBufferHandlingEnabled) {
        m_streamer->setDelayedBufferHandlingEnabled(appState.options.delayedBufferHandlingEnabled);
        LOG_INFO("Buffer handling: %s", appState.options.delayedBufferHandlingEnabled ? "DELAYED" : "IMMEDIATE");
    }

    if (force || appState.options.undistortEnabled != prevState.options.undistortEnabled) {
        LOG_INFO("Color stream undistortion: %s", appState.options.undistortEnabled ? "ENABLED" : "DISABLED");
    }

    // Data stream: YUV
    if (force || appState.options.dataStreamColorEnabled != prevState.options.dataStreamColorEnabled) {
        const auto streamType = varjo_StreamType_DistortedColor;
        const auto streamFormat = m_colorStreamFormat;
        const auto streamChannels = varjo_ChannelFlag_Left | varjo_ChannelFlag_Right;
        varjo_ChannelFlag currentChannels = varjo_ChannelFlag_None;

        if (appState.options.dataStreamColorEnabled) {
            if (m_streamer->isStreaming(streamType, streamFormat, currentChannels)) {
                if (currentChannels == streamChannels) {
                    // If running stream channels match, match we want to stop it
                    m_streamer->stopDataStream(streamType, streamFormat);
                    if (appState.options.vrColorCorrectionEnabled) {
                        // If color correction is enabled, start stream with no data channels to
                        // keep getting camera metadata for exposure adjustment
                        LOG_INFO("Switching to metadata only color stream..");
                        m_streamer->startDataStream(streamType, streamFormat, varjo_ChannelFlag_None);
                    }
                } else {
                    // When color correction is enabled, stop stream and start new stream with data channels
                    LOG_INFO("Switching to color stream with data channels..");
                    assert(appState.options.vrColorCorrectionEnabled);
                    m_streamer->stopDataStream(streamType, streamFormat);
                    m_streamer->startDataStream(streamType, streamFormat, streamChannels);
                }
            } else {
                // No streams running, just start our color data stream
                m_streamer->startDataStream(streamType, streamFormat, streamChannels);
            }
        } else {
            if (m_streamer->isStreaming(streamType, streamFormat, currentChannels)) {
                if (currentChannels == streamChannels) {
                    // If running stream channels match, match we want to stop it
                    m_streamer->stopDataStream(streamType, streamFormat);
                    if (appState.options.vrColorCorrectionEnabled) {
                        // If color correction is enabled, start stream with no data channels to
                        // keep getting camera metadata for exposure adjustment
                        LOG_INFO("Switching to metadata only color stream..");
                        m_streamer->startDataStream(streamType, streamFormat, varjo_ChannelFlag_None);
                    }
                } else {
                    // When color correction is enabled, stop stream and start new stream with data channels
                    LOG_INFO("Switching to color stream with data channels..");
                    assert(appState.options.vrColorCorrectionEnabled);
                    m_streamer->stopDataStream(streamType, streamFormat);
                    m_streamer->startDataStream(streamType, streamFormat, streamChannels);
                }
            } else {
                // No streams running, just ignore
            }

            // Reset textures if disabled
            {
                std::lock_guard<std::mutex> streamLock(m_frameDataMutex);
                m_frameData.colorFrames = {};
                m_scene->updateColorFrame(0, {0, 0}, 0, 0, nullptr);
                m_scene->updateColorFrame(1, {0, 0}, 0, 0, nullptr);
            }
        }

        // Write stream status back to state
        m_appState.options.dataStreamColorEnabled = m_streamer->isStreaming(streamType, streamFormat);
    }

    // Data stream: Cubemap
    if (force || appState.options.dataStreamCubemapEnabled != prevState.options.dataStreamCubemapEnabled) {
        const auto streamType = varjo_StreamType_EnvironmentCubemap;
        const auto streamFormat = varjo_TextureFormat_RGBA16_FLOAT;
        const auto streamChannels = varjo_ChannelFlag_First;

        if (appState.options.dataStreamCubemapEnabled) {
            if (!m_streamer->isStreaming(streamType, streamFormat)) {
                m_streamer->startDataStream(streamType, streamFormat, streamChannels);
            }
        } else {
            if (m_streamer->isStreaming(streamType, streamFormat)) {
                m_streamer->stopDataStream(streamType, streamFormat);
            }

            // Reset textures if disabled
            {
                std::lock_guard<std::mutex> streamLock(m_frameDataMutex);
                m_frameData.cubemapFrame = {};
                m_scene->updateHdrCubemap(0, 0, 0, nullptr);
            }
        }

        // Write stream status back to state
        m_appState.options.dataStreamCubemapEnabled = m_streamer->isStreaming(streamType, streamFormat);
    }

    // Frame rate limiter
    if (force || (appState.options.vrLimitFrameRate != prevState.options.vrLimitFrameRate)) {
        LOG_INFO("Frame rate limiter: %s", (appState.options.vrLimitFrameRate ? "ON" : "OFF"));
    }
}

void AppLogic::onFrameReceived(const DataStreamer::Frame& frame)
{
    const auto& streamFrame = frame.metadata.streamFrame;

    std::lock_guard<std::mutex> streamLock(m_frameDataMutex);
    switch (streamFrame.type) {
        case varjo_StreamType_DistortedColor: {
            // We update color stream metadata from first channel only
            if (frame.metadata.channelIndex == varjo_ChannelIndex_First) {
                m_frameData.metadata = streamFrame.metadata.distortedColor;
            }
            m_frameData.colorFrames[static_cast<size_t>(frame.metadata.channelIndex)] = frame;
        } break;
        case varjo_StreamType_EnvironmentCubemap: {
            // We update cube frame only from first channel only (actually there is no second)
            if (frame.metadata.channelIndex == varjo_ChannelIndex_First) {
                m_frameData.cubemapFrame = frame;
                m_frameData.cubemapMetadata = streamFrame.metadata.environmentCubemap;
            }
        } break;
        default: {
            LOG_ERROR("Unsupported stream type: %d", streamFrame.type);
            assert(false);
        } break;
    }
}

void AppLogic::update()
{
    // Check for new mixed reality events
    checkEvents();

    // Handle delayed data stream buffers
    m_streamer->handleDelayedBuffers();

    // Sync frame
    m_varjoView->syncFrame();

    // Update frame time
    m_appState.general.frameTime += m_varjoView->getDeltaTime();
    m_appState.general.frameCount = m_varjoView->getFrameNumber();

    // Get latest frame data
    FrameData frameData{};
    {
        std::lock_guard<std::mutex> streamLock(m_frameDataMutex);

        frameData.metadata = m_frameData.metadata;

        // Move frame datas
        for (size_t ch = 0; ch < m_frameData.colorFrames.size(); ch++) {
            frameData.colorFrames[ch] = std::move(m_frameData.colorFrames[ch]);
            m_frameData.colorFrames[ch] = std::nullopt;
        }

        frameData.cubemapFrame = std::move(m_frameData.cubemapFrame);
        m_frameData.cubemapFrame = std::nullopt;

        frameData.cubemapMetadata = m_frameData.cubemapMetadata;
    }

    // Update scene
    MRScene::UpdateParams updateParams{};

    // Determine current cubemap mode. If cubemap stream is not enabled, we default to the fixed 6500K mode to allow VR color correction.
    varjo_EnvironmentCubemapMode cubemapMode = varjo_EnvironmentCubemapMode_Fixed6500K;

    if (m_appState.options.dataStreamCubemapEnabled && frameData.cubemapMetadata.has_value()) {
        cubemapMode = frameData.cubemapMetadata.value().mode;
    }

    // Get latest color adjustments. These will take effect only if cubemap is not using auto adaptation.
    if (cubemapMode != varjo_EnvironmentCubemapMode_AutoAdapt) {
        updateParams.cameraParams.simulateBrightness = true;

        if (m_appState.options.vrColorCorrectionEnabled && m_appState.options.videoRenderingEnabled) {
            if (frameData.metadata.has_value()) {
                const auto& metadata = *frameData.metadata;
                updateParams.cameraParams.exposureEV = metadata.ev;
                updateParams.cameraParams.cameraCalibrationConstant = metadata.cameraCalibrationConstant;
                updateParams.cameraParams.wbNormalizationData = metadata.wbNormalizationData;
            }
        }

        updateParams.lighting.ambientLight = m_appState.options.ambientLightGainRGB;
    }

    m_scene->update(m_varjoView->getFrameTime(), m_varjoView->getDeltaTime(), m_varjoView->getFrameNumber(), updateParams);

    // Get latest cubemap frame.
    if (frameData.cubemapFrame.has_value()) {
        const auto& cubemapFrame = frameData.cubemapFrame.value();
        m_scene->updateHdrCubemap(cubemapFrame.metadata.bufferMetadata.width, cubemapFrame.metadata.bufferMetadata.format,
            cubemapFrame.metadata.bufferMetadata.rowStride, cubemapFrame.data.data());
    }

    // Get latest color frame
    for (size_t ch = 0; ch < frameData.colorFrames.size(); ch++) {
        if (frameData.colorFrames[ch].has_value()) {
            const auto& colorFrame = frameData.colorFrames[ch].value();

            if (colorFrame.metadata.bufferMetadata.byteSize == 0) {
                continue;
            }

            if (m_appState.options.undistortEnabled) {
                constexpr int c_downScaleFactor = 4;
                const auto w = colorFrame.metadata.bufferMetadata.width / c_downScaleFactor;
                const auto h = colorFrame.metadata.bufferMetadata.height / c_downScaleFactor;
                const auto rowStride = w * 4;

                std::optional<varjo_Matrix> projection = std::nullopt;
                std::vector<uint8_t> bufferRGBA(rowStride * h);
                DataStreamer::convertDistortedYUVToRectifiedRGBA(colorFrame.metadata.bufferMetadata, colorFrame.data.data(), glm::ivec2(w, h),
                    bufferRGBA.data(), colorFrame.metadata.extrinsics, colorFrame.metadata.intrinsics, projection);

                sendImageToServer(sock, bufferRGBA, w, h);

                m_scene->updateColorFrame(static_cast<int>(ch), glm::ivec2(w, h), varjo_TextureFormat_R8G8B8A8_UNORM, rowStride, bufferRGBA.data());
            }
            else {
                const auto w = colorFrame.metadata.bufferMetadata.width;
                const auto h = colorFrame.metadata.bufferMetadata.height;
                const auto rowStride = w * 4;

                std::vector<uint8_t> bufferRGBA(rowStride * h);
                DataStreamer::convertToR8G8B8A(colorFrame.metadata.bufferMetadata, colorFrame.data.data(), bufferRGBA.data());

                sendImageToServer(sock, bufferRGBA, w, h);

                m_scene->updateColorFrame(static_cast<int>(ch), glm::ivec2(w, h), varjo_TextureFormat_R8G8B8A8_UNORM, rowStride, bufferRGBA.data());
            }
        }
    }

    // Early exit if no frame submit
    if (!m_appState.options.renderVREnabled) {
        // Invalidate frame by submitting empty frame on the first call, then no op
        m_varjoView->invalidateFrame();
        return;
    }

    // Limit frame rate
    if (m_appState.options.vrLimitFrameRate) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 45));
    }

    // Begin frame
    m_varjoView->beginFrame();

    // Render layer
    {
        // Get layer for rendering
        constexpr int layerIndex = 0;
        auto& layer = m_varjoView->getLayer(layerIndex);

        // Setup render params
        MultiLayerView::Layer::SubmitParams submitParams{};
        submitParams.submitColor = m_appState.options.renderVREnabled;
        submitParams.submitDepth = m_appState.options.submitVRDepthEnabled;
        submitParams.depthTestEnabled = m_appState.options.VideoDepthEstimationEnabled;
        submitParams.depthTestRangeEnabled = m_appState.options.vrDepthTestRangeEnabled;
        submitParams.depthTestRangeLimits = {
            0.0, submitParams.depthTestRangeEnabled ? std::max(static_cast<double>(m_appState.options.vrDepthTestRangeValue), 0.0) : 0.0 };
        submitParams.chromaKeyEnabled = m_appState.options.chromaKeyingEnabled;
        submitParams.alphaBlend = m_appState.options.videoRenderingEnabled || !m_appState.options.drawVRBackgroundEnabled;

        // Begin layer rendering
        layer.begin(submitParams);

        // Clear frame
        const glm::vec4 opaqueBg(0.15f, 0.5f, 0.6f, 1.0f);
        const glm::vec4 transpBg(0.0f, 0.0f, 0.0f, 0.0f);
        const glm::vec4 clearColor = submitParams.alphaBlend ? transpBg : opaqueBg;
        layer.clear(MultiLayerView::Layer::ClearParams(clearColor));

        // Render frame
        layer.renderScene(*m_scene);

        // End layer rendering
        layer.end();
    }

    // Submit varjo frame with rendered layers
    m_varjoView->endFrame();
}


void AppLogic::onMixedRealityAvailable(bool available, bool forceSetState)
{
    m_appState.general.mrAvailable = available;

    if (available) {
        // Update stuff here if needed

        // Get format for color stream
        m_colorStreamFormat = m_streamer->getFormat(varjo_StreamType_DistortedColor);

        if (m_appState.options.reactToConnectionEvents && !m_appState.options.videoRenderingEnabled) {
            LOG_INFO("Enabling video rendering on MR available event..");
            setVideoRendering(true);
        }

    } else {
        LOG_ERROR("Mixed Reality features not available.");

        // Update stuff here if needed
        if (m_appState.options.reactToConnectionEvents && m_appState.options.videoRenderingEnabled) {
            LOG_INFO("Disabling video rendering on MR unavailable event..");
            setVideoRendering(false);
        }
    }

    // Force set state when MR becomes active
    if (forceSetState) {
        setState(m_appState, true);
    }

    // Update camera properties based on the mixed reality availability
    m_camera->enumerateCameraProperties(available);
}

void AppLogic::checkEvents()
{
    varjo_Bool ret = varjo_False;

    do {
        varjo_Event evt{};
        ret = varjo_PollEvent(m_session, &evt);
        CHECK_VARJO_ERR(m_session);

        if (ret == varjo_True) {
            switch (evt.header.type) {
                case varjo_EventType_MRDeviceStatus: {
                    switch (evt.data.mrDeviceStatus.status) {
                        // Occurs when Mixed Reality features are enabled
                        case varjo_MRDeviceStatus_Connected: {
                            LOG_INFO("EVENT: Mixed reality device status: %s", "Connected");
                            constexpr bool forceSetState = true;
                            onMixedRealityAvailable(true, forceSetState);
                        } break;
                        // Occurs when Mixed Reality features are disabled
                        case varjo_MRDeviceStatus_Disconnected: {
                            LOG_INFO("EVENT: Mixed reality device status: %s", "Disconnected");
                            constexpr bool forceSetState = false;
                            onMixedRealityAvailable(false, forceSetState);
                        } break;
                        default: {
                            // Ignore unknown status.
                        } break;
                    }
                } break;

                case varjo_EventType_MRCameraPropertyChange: {
                    varjo_CameraPropertyMode mode = varjo_MRGetCameraPropertyMode(m_session, evt.data.mrCameraPropertyChange.type);
                    varjo_CameraPropertyValue manualValue = varjo_MRGetCameraPropertyValue(m_session, evt.data.mrCameraPropertyChange.type);
                    std::string cameraPropStr = CameraManager::propertyTypeToString(evt.data.mrCameraPropertyChange.type);
                    std::string modeStr = CameraManager::propertyModeToString(mode);
                    std::string valStr = CameraManager::propertyValueToString(evt.data.mrCameraPropertyChange.type, manualValue);
                    LOG_INFO("EVENT: Camera prop changed: %s. mode: %s, value: %s", cameraPropStr.c_str(), modeStr.c_str(), valStr.c_str());

                    // Update any information related to the changed property
                    getCamera().onCameraPropertyChanged(evt.data.mrCameraPropertyChange.type);
                } break;

                case varjo_EventType_DataStreamStart: {
                    LOG_INFO("EVENT: Data stream started: id=%d", static_cast<int>(evt.data.dataStreamStart.streamId));
                } break;

                case varjo_EventType_DataStreamStop: {
                    LOG_INFO("EVENT: Data stream stopped: id=%d", static_cast<int>(evt.data.dataStreamStop.streamId));
                } break;

                case varjo_EventType_MRChromaKeyConfigChange: {
                    LOG_INFO("EVENT: Chroma key config changed");
                } break;

                default: {
                    // Ignore unknown event.
                } break;
            }
        }
    } while (ret);
}

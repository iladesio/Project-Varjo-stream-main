// Copyright 2019-2020 Varjo Technologies Oy. All rights reserved.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "DataStreamer.hpp"

#include <fstream>
#include <string>
#include <algorithm>

#include <DirectXPackedVector.h>
#include <glm/gtc/type_ptr.hpp>
#include "Undistorter.hpp"

namespace VarjoExamples
{
namespace
{
// Stream stats interval
const auto c_reportInterval = std::chrono::seconds{1};

// Channel flags for channel indices
const varjo_ChannelFlag c_channelFlags[] = {varjo_ChannelFlag_First, varjo_ChannelFlag_Second};

// Converts YUV color to RGB
inline void convertYUVtoRGB(uint8_t Y, uint8_t U, uint8_t V, uint8_t& R, uint8_t& G, uint8_t& B)
{
    const int C = static_cast<int>(Y) - 16;
    const int D = static_cast<int>(U) - 128;
    const int E = static_cast<int>(V) - 128;
    R = static_cast<uint8_t>(std::clamp((298 * C + 409 * E + 128) >> 8, 0, 255));
    G = static_cast<uint8_t>(std::clamp((298 * C - 100 * D - 208 * E + 128) >> 8, 0, 255));
    B = static_cast<uint8_t>(std::clamp((298 * C + 516 * D + 128) >> 8, 0, 255));
}

// Converts color from Y8 format to RGBA
constexpr uint32_t convertY8toRGBA(uint8_t Y)
{
    const int C = static_cast<int>(Y) - 16;
    const uint32_t gray = static_cast<uint32_t>(std::clamp((298 * C + 128) >> 8, 0, 255));
    return gray | (gray << 8) | (gray << 16) | 0xff000000;
}

// Helper template to construct Y8 to RGBA map
template <uint32_t... I>
static constexpr std::array<uint32_t, sizeof...(I)> buildY8toRGBAMap(std::index_sequence<I...>) noexcept
{
    return std::array<uint32_t, sizeof...(I)>{convertY8toRGBA(I)...};
}

// Map for doing optimized color conversion from Y8 to RGBA
constexpr std::array<uint32_t, 256> c_convertY8ToRGBAMap = buildY8toRGBAMap(std::make_index_sequence<256>{});

void writeBMP(const std::string& filename, int32_t width, int32_t height, int32_t components, const uint8_t* data)
{
    assert(components == 4);

    std::ofstream outFile(filename, std::ofstream::binary);
    if (!outFile.good()) {
        LOG_ERROR("Opening file for writing failed: %s", filename.c_str());
        return;
    }

    const uint32_t imageDataSize = components * width * height;

    // Write BMP headers
    BITMAPFILEHEADER bmFileHdr;
    bmFileHdr.bfType = *(reinterpret_cast<const WORD*>("BM"));
    bmFileHdr.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imageDataSize;
    bmFileHdr.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    outFile.write(reinterpret_cast<const char*>(&bmFileHdr), sizeof(bmFileHdr));
    if (!outFile.good()) {
        LOG_ERROR("Writing to bitmap file failed: %s", filename.c_str());
        return;
    }

    // Write bitmap header for RGB data
    BITMAPINFOHEADER bmInfoHdr;
    ZeroMemory(&bmInfoHdr, sizeof(bmInfoHdr));
    bmInfoHdr.biSize = sizeof(bmInfoHdr);
    bmInfoHdr.biWidth = width;
    bmInfoHdr.biHeight = -height;  // Negative height to avoid flipping image vertically
    bmInfoHdr.biPlanes = 1;
    bmInfoHdr.biBitCount = 32;
    bmInfoHdr.biCompression = BI_RGB;
    bmInfoHdr.biSizeImage = 0;
    bmInfoHdr.biXPelsPerMeter = bmInfoHdr.biYPelsPerMeter = 2835;
    bmInfoHdr.biClrImportant = bmInfoHdr.biClrUsed = 0;
    outFile.write(reinterpret_cast<const char*>(&bmInfoHdr), sizeof(bmInfoHdr));
    if (!outFile.good()) {
        LOG_ERROR("Writing to bitmap file failed: %s", filename.c_str());
        return;
    }

    // Write data row by row
    std::vector<uint8_t> row(components * width);
    for (int32_t y = 0; y < height; ++y) {
        const uint8_t* src = data + y * width * components;
        uint8_t* dst = row.data();
        for (int32_t x = 0; x < width; ++x) {
            // Swap RGBA to BGRA used in bitmaps
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
            dst += 4;
            src += 4;
        }

        outFile.write(reinterpret_cast<const char*>(row.data()), row.size());
        if (!outFile.good()) {
            LOG_ERROR("Writing to bitmap file failed: %s", filename.c_str());
            return;
        }
    }

    outFile.close();
    LOG_INFO("File saved succesfully: %s", filename.c_str());
}

// Save varjo buffer data as BMP image file
void saveBMP(const std::string& filename, const varjo_BufferMetadata& buffer, const void* bufferData)
{
    LOG_DEBUG("Saving buffer to file: %s", filename.c_str());

    constexpr int32_t components = 4;
    std::vector<uint8_t> output(buffer.width * buffer.height * components);

    DataStreamer::convertToR8G8B8A(buffer, bufferData, output.data());

    writeBMP(filename.c_str(), buffer.width, buffer.height, components, output.data());
}

}  // namespace

DataStreamer::DataStreamer(varjo_Session* session, const std::function<void(const Frame&)>& onFrameCallback)
    : m_session(session)
    , m_onFrameCallback(onFrameCallback)
{
    m_streamManagement.running = true;
}

DataStreamer::~DataStreamer()
{
    // To initiate shutdown, set running flag to false to ensure all callbacks will not process data anymore.
    // We need to lock the mutex only for the time when the flag is being changed.
    {
        std::lock_guard<std::recursive_mutex> streamLock(m_streamManagement.mutex);
        m_streamManagement.running = false;
    }

    // If we have streams running, stop them
    for (const auto& [streamId, stream] : m_streamManagement.streams) {
        LOG_WARNING("Stopping running data stream: %d", static_cast<int>(streamId));
        varjo_StopDataStream(m_session, streamId);
    }

    // Reset session
    m_session = nullptr;
}

std::pair<varjo_StreamId, varjo_ChannelFlag> DataStreamer::getStreamingIdAndChannel(varjo_StreamType streamType, varjo_TextureFormat streamFormat) const
{
    std::pair<varjo_StreamId, varjo_ChannelFlag> streamInfo = std::make_pair(varjo_InvalidId, varjo_ChannelFlag_None);
    {
        // Frame callback comes from different thread, lock streaming data
        std::lock_guard<std::recursive_mutex> streamLock(m_streamManagement.mutex);

        // Find out if we have running stream
        const auto it = std::find_if(m_streamManagement.streams.begin(), m_streamManagement.streams.end(),
            [streamType, streamFormat](const std::pair<varjo_StreamId, StreamData>& item) {
                return item.second.streamType == streamType && item.second.streamFormat == streamFormat;
            });

        if (it != m_streamManagement.streams.end()) {
            streamInfo = std::make_pair(it->first, it->second.channels);
        }
    }
    return streamInfo;
}

bool DataStreamer::isStreaming() const
{
    // Frame callback comes from different thread, lock streaming data
    std::lock_guard<std::recursive_mutex> streamLock(m_streamManagement.mutex);

    // Find out if we have running streams
    return !m_streamManagement.streams.empty();
}

bool DataStreamer::isStreaming(varjo_StreamType streamType) const
{
    // Lock streaming data
    std::lock_guard<std::recursive_mutex> streamLock(m_streamManagement.mutex);

    const auto it = std::find_if(m_streamManagement.streams.begin(), m_streamManagement.streams.end(),
        [streamType](const std::pair<varjo_StreamId, StreamData>& item) { return item.second.streamType == streamType; });

    return it != m_streamManagement.streams.end();
}

bool DataStreamer::isStreaming(varjo_StreamType streamType, varjo_TextureFormat streamFormat) const
{
    return (getStreamingIdAndChannel(streamType, streamFormat).first != varjo_InvalidId);
}

std::optional<varjo_StreamConfig> DataStreamer::getConfig(varjo_StreamType streamType) const
{
    std::vector<varjo_StreamConfig> configs;
    configs.resize(varjo_GetDataStreamConfigCount(m_session));
    varjo_GetDataStreamConfigs(m_session, configs.data(), static_cast<int32_t>(configs.size()));
    if (CHECK_VARJO_ERR(m_session) == varjo_NoError) {
        for (const auto& config : configs) {
            if (config.streamType == streamType) {
                return config;
            }
        }
    }

    return {};
}

varjo_TextureFormat DataStreamer::getFormat(varjo_StreamType streamType) const
{
    const auto optStreamConfig = getConfig(streamType);
    return optStreamConfig.has_value() ? optStreamConfig->format : varjo_TextureFormat_INVALID;
}

bool DataStreamer::isStreaming(varjo_StreamType streamType, varjo_TextureFormat streamFormat, varjo_ChannelFlag& outChannels) const
{
    const auto streamingInfo = getStreamingIdAndChannel(streamType, streamFormat);
    outChannels = streamingInfo.second;
    return (streamingInfo.first != varjo_InvalidId);
}

void DataStreamer::startDataStream(varjo_StreamType streamType, varjo_TextureFormat streamFormat, varjo_ChannelFlag channels)
{
    varjo_StreamId streamId = getStreamingIdAndChannel(streamType, streamFormat).first;

    if (streamId == varjo_InvalidId) {
        LOG_INFO("Start streaming: type=%lld, format=%lld", streamType, streamFormat);

        if (!isStreaming()) {
            m_statusLine = "Starting stream.";
        }

        // Start stream
        streamId = startStreaming(streamType, streamFormat, channels);

        // Check if successfully started
        if (streamId != varjo_InvalidId) {
            // Frame callback comes from different thread, lock streaming data
            std::lock_guard<std::recursive_mutex> streamLock(m_streamManagement.mutex);

            StreamData stream;
            stream.streamId = streamId;
            stream.streamType = streamType;
            stream.streamFormat = streamFormat;
            stream.channels = channels;
            stream.frameData[varjo_ChannelIndex_First] = {};
            stream.frameData[varjo_ChannelIndex_Second] = {};
            m_streamManagement.streams[streamId] = stream;

            // Reset stats if first stream
            if (m_streamManagement.streams.size() == 1) {
                m_stats = {};
                m_stats.reportTime = std::chrono::high_resolution_clock::now();
            }
        } else {
            LOG_WARNING("Start stream failed. Could not find stream with type=%lld, format=%lld", streamType, streamFormat);
        }
    } else {
        LOG_WARNING("Start stream failed. Already running: type=%lld, format=%lld", streamType, streamFormat);
    }
}

void DataStreamer::stopDataStream(varjo_StreamType streamType, varjo_TextureFormat streamFormat)
{
    varjo_StreamId streamId = getStreamingIdAndChannel(streamType, streamFormat).first;

    if (streamId != varjo_InvalidId) {
        LOG_INFO("Stop streaming: type=%lld", streamType);

        // Stop stream
        varjo_StopDataStream(m_session, streamId);
        CHECK_VARJO_ERR(m_session);

        // Scope lock for cleanup
        {
            // Frame callback comes from different thread, lock streaming data
            std::lock_guard<std::recursive_mutex> streamLock(m_streamManagement.mutex);

            // Remove stream
            m_streamManagement.streams.erase(streamId);

            // Remove any delayed buffer that we might have stored. Buffers were already
            // freed by varjo_StopDataStream call.
            const auto isBufferFromThisStream = [streamId](const DelayedBuffer& delayedBuffer) { return delayedBuffer.frame.streamFrame.id == streamId; };
            auto& delayedBuffers = m_streamManagement.delayedBuffers;
            delayedBuffers.erase(std::remove_if(delayedBuffers.begin(), delayedBuffers.end(), isBufferFromThisStream), delayedBuffers.end());
        }

        if (!isStreaming()) {
            m_statusLine = "";
        }

    } else {
        LOG_WARNING("Stop stream failed. Not running: type=%lld, format=%lld", streamType, streamFormat);
    }
}

void DataStreamer::handleDelayedBuffers(bool ignore)
{
    // Callback comes from different thread, lock streaming data
    std::lock_guard<std::recursive_mutex> streamLock(m_streamManagement.mutex);

    auto& delayedBuffers = m_streamManagement.delayedBuffers;
    if (delayedBuffers.empty()) {
        return;
    }

    // Handle buffers if not ignored
    if (!ignore) {
        LOG_DEBUG("Handling delayed stream buffers: count=%d", delayedBuffers.size());
        for (auto& db : delayedBuffers) {
            storeBuffer(db.frame, db.bufferId, db.cpuBuffer, db.baseName, db.takeSnapshot);
        }
    } else {
        LOG_DEBUG("Ignoring delayed stream buffers: count=%d", delayedBuffers.size());

        for (auto& db : delayedBuffers) {
            if (db.bufferId != varjo_InvalidId) {
                // Just unlock buffer to allow reuse
                LOG_DEBUG("Unlocking buffer (id=%lld)", db.bufferId);
                varjo_UnlockDataStreamBuffer(m_session, db.bufferId);
                CHECK_VARJO_ERR(m_session);
            }
        }
    }

    // Clear buffers
    delayedBuffers.clear();
}

void DataStreamer::printStreamConfigs() const
{
    std::vector<varjo_StreamConfig> configs;
    configs.resize(varjo_GetDataStreamConfigCount(m_session));
    varjo_GetDataStreamConfigs(m_session, configs.data(), static_cast<int32_t>(configs.size()));
    CHECK_VARJO_ERR(m_session);

    LOG_INFO("\nStream configs:");
    for (const auto& config : configs) {
        LOG_INFO("  Stream: id=%lld, type=%lld, bufferType=%lld, format=%lld, channels=%lld, fps=%d, w=%d, h=%d, stride=%d", config.streamId, config.streamType,
            config.bufferType, config.format, config.channelFlags, config.frameRate, config.width, config.height, config.rowStride);
    }
    LOG_INFO("");
}

void DataStreamer::storeBuffer(const Frame::Metadata& frameMetadata, varjo_BufferId bufferId, void* cpuData, const std::string& baseName, bool takeSnapshot)
{
    auto streamIt = m_streamManagement.streams.find(frameMetadata.streamFrame.id);
    if (streamIt == m_streamManagement.streams.end()) {
        // Stream has been stopped and removed already
        return;
    }

    auto channelIt = streamIt->second.frameData.find(frameMetadata.channelIndex);
    if (channelIt == streamIt->second.frameData.end()) {
        // Removed or unsupported channel
        return;
    }

    // Handle buffer
    auto& frameData = channelIt->second;
    bool validFrameData = false;
    if (bufferId == varjo_InvalidId) {
        // Metadata only
        assert(cpuData == nullptr);
        assert(frameMetadata.bufferMetadata.byteSize == 0);
        validFrameData = true;
    } else if (frameMetadata.bufferMetadata.type == varjo_BufferType_CPU) {
        assert(cpuData);
        assert(frameMetadata.bufferMetadata.format == varjo_TextureFormat_RGBA16_FLOAT ||  //
               frameMetadata.bufferMetadata.format == varjo_TextureFormat_NV12 ||          //
               frameMetadata.bufferMetadata.format == varjo_TextureFormat_Y8_UNORM);

        if (takeSnapshot) {
            // Save buffer data to file.
            std::string fileName = baseName + "_sid" + std::to_string(frameMetadata.streamFrame.id) + "_frm" +
                                   std::to_string(frameMetadata.streamFrame.frameNumber) + "_bid" + std::to_string(bufferId) + ".bmp";
            saveBMP(fileName, frameMetadata.bufferMetadata, cpuData);
        }

        validFrameData = true;
    } else if (frameMetadata.bufferMetadata.type == varjo_BufferType_GPU) {
        assert(cpuData == nullptr);
        CRITICAL("GPU buffers not currently supported!");
    } else {
        CRITICAL("Unsupported output type!");
    }

    if (validFrameData && m_onFrameCallback) {
        // Store the frame
        auto& frame = frameData.frame;

        // Store metadata
        frame.metadata = frameMetadata;

        // Resize buffer if needed.
        if (frame.data.size() != frameMetadata.bufferMetadata.byteSize) {
            frame.data.resize(frameMetadata.bufferMetadata.byteSize);
        }

        // Store buffer data
        memcpy(frame.data.data(), cpuData, frameMetadata.bufferMetadata.byteSize);

        // Do callback
        m_onFrameCallback(frame);
    }
    frameData.frameCount++;

    // Unlock buffer
    if (bufferId != varjo_InvalidId) {
        LOG_DEBUG("Unlocking buffer (id=%lld)", bufferId);
        varjo_UnlockDataStreamBuffer(m_session, bufferId);
        CHECK_VARJO_ERR(m_session);
    }
}

void DataStreamer::handleBuffer(const Frame::Metadata& frameMetadata, varjo_BufferId bufferId, const std::string& baseName, bool takeSnapshot)
{
    varjo_BufferMetadata bufferMetadata{};
    void* cpuData = nullptr;

    if (bufferId != varjo_InvalidId) {
        // Lock buffer if we have one (metadata only streams don't have it)
        varjo_LockDataStreamBuffer(m_session, bufferId);
        CHECK_VARJO_ERR(m_session);

        bufferMetadata = varjo_GetBufferMetadata(m_session, bufferId);
        cpuData = varjo_GetBufferCPUData(m_session, bufferId);

        LOG_DEBUG("Locked buffer (id=%lld): res=%dx%d, stride=%u, bytes=%u, type=%d, format=%d", bufferId, bufferMetadata.width, bufferMetadata.height,
            bufferMetadata.rowStride, bufferMetadata.byteSize, (int)bufferMetadata.type, (int)bufferMetadata.format);
    }

    bool delayed = m_delayedBufferHandling;

    Frame::Metadata frame = frameMetadata;
    frame.bufferMetadata = bufferMetadata;

    if (delayed) {
        DelayedBuffer delayedBuffer;
        delayedBuffer.frame = frame;
        delayedBuffer.bufferId = bufferId;
        delayedBuffer.cpuBuffer = cpuData;
        delayedBuffer.baseName = baseName;
        delayedBuffer.takeSnapshot = takeSnapshot;

        // Add to delayed buffers. Will be handled in main loop.
        m_streamManagement.delayedBuffers.emplace_back(delayedBuffer);

    } else {
        // Handle buffer immediately
        storeBuffer(frame, bufferId, cpuData, baseName, takeSnapshot);
    }
}

void DataStreamer::dataStreamFrameCallback(const varjo_StreamFrame* frame, varjo_Session* session, void* userData)
{
    // This callback is called by Varjo runtime from a separate stream specific thread.
    // To avoid dropping frames, the callback should be as lightweight as possible.
    // i.e. file writing should be offloaded to a different thread.

    DataStreamer* streamer = reinterpret_cast<DataStreamer*>(userData);
    streamer->onDataStreamFrame(frame, session);
}

void DataStreamer::onDataStreamFrame(const varjo_StreamFrame* frame, varjo_Session* session)
{
    // Callback comes from different thread, lock streaming data
    std::lock_guard<std::recursive_mutex> streamLock(m_streamManagement.mutex);

    // Check if the data streaming is shutting down
    if (!m_streamManagement.running) {
        return;
    }

    m_stats.frameCount++;
    const auto now = std::chrono::high_resolution_clock::now();
    const auto delta = now - m_stats.reportTime;
    if (delta >= c_reportInterval) {
        m_statusLine = "Got " + std::to_string(m_stats.frameCount) + " frames from " + std::to_string(m_streamManagement.streams.size()) + " streams in last " +
                       std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()) + " ms";
        m_stats = {};
        m_stats.reportTime = now;
    }

    // Check that client session hasn't already be reset in destructor. Should never happen!
    if (session != m_session) {
        LOG_ERROR("Invalid session in callback.");
        return;
    }

    // Check that the stream is still running
    const auto it = m_streamManagement.streams.find(frame->id);
    if (it == m_streamManagement.streams.end()) {
        LOG_WARNING("Frame callback ignored. Stream already deleted: type=%lld, id=%lld", frame->type, frame->id);
        return;
    }

    auto& stream = it->second;

    // Check wether we need to take snapshot of the frame
    const auto snaphotRequested = stream.snapshotRequested;
    it->second.snapshotRequested = false;

    // Buffer filename prefixes
    std::vector<std::string> bufferFilenames;
    varjo_Nanoseconds timestamp = 0;
    switch (frame->type) {
        case varjo_StreamType_DistortedColor:
            LOG_DEBUG("Got frame #%lld: id=%lld, type=%lld, time=%.3f, exposure=%.2f, ev=%.2f, temp=%.2f, rgb=(%.2f, %.2f, %.2f)", frame->frameNumber,
                frame->id, frame->type, 1E-9 * frame->metadata.distortedColor.timestamp, frame->metadata.distortedColor.exposureTime,
                frame->metadata.distortedColor.ev, frame->metadata.distortedColor.whiteBalanceTemperature,
                frame->metadata.distortedColor.wbNormalizationData.whiteBalanceColorGains[0],
                frame->metadata.distortedColor.wbNormalizationData.whiteBalanceColorGains[1],
                frame->metadata.distortedColor.wbNormalizationData.whiteBalanceColorGains[2]);

            timestamp = frame->metadata.distortedColor.timestamp;

            bufferFilenames = {"rgbLeft", "rgbRight"};
            break;

        case varjo_StreamType_EnvironmentCubemap:
            LOG_DEBUG("Got frame #%lld: id=%lld, type=%lld, time=%.3f", frame->frameNumber, frame->id, frame->type,
                1E-9 * frame->metadata.environmentCubemap.timestamp);

            if (!(frame->channels & varjo_ChannelFlag_First)) {
                LOG_WARNING("    (missing first buffer)");
                return;
            }

            timestamp = frame->metadata.environmentCubemap.timestamp;

            // Use a distinct name for the file in case auto adaptation was enabled.
            if (frame->metadata.environmentCubemap.mode == varjo_EnvironmentCubemapMode_AutoAdapt) {
                bufferFilenames = {"cube_adapted"};
            } else {
                bufferFilenames = {"cube"};
            }
            break;

        case varjo_StreamType_EyeCamera:
            LOG_DEBUG("Got frame #%lld: id=%lld, type=%lld, time=%.3f, glint LEDs=(%x, %x)", frame->frameNumber, frame->id, frame->type,
                1E-9 * frame->metadata.eyeCamera.timestamp, frame->metadata.eyeCamera.glintMaskLeft, frame->metadata.eyeCamera.glintMaskRight);
            timestamp = frame->metadata.eyeCamera.timestamp;
            bufferFilenames = {"eyeLeft", "eyeRight"};
            break;

        default: {
            CRITICAL("Unsupported stream type: %lld", frame->type);
            return;
        };
    }

    // Handle metadata only streams
    if (stream.channels == varjo_ChannelFlag_None) {
        Frame::Metadata frameMetadata;
        frameMetadata.streamFrame = *frame;
        frameMetadata.channelIndex = varjo_ChannelIndex_Left;
        frameMetadata.timestamp = timestamp;
        frameMetadata.extrinsics = {};
        frameMetadata.intrinsics = {};
        handleBuffer(frameMetadata, varjo_InvalidId, "", false);
        return;
    }

    // Handle streams with image data
    for (varjo_ChannelIndex channelIndex : {varjo_ChannelIndex_Left, varjo_ChannelIndex_Right}) {
        if (!(frame->channels & (1ull << channelIndex))) {
            continue;
        }

        LOG_DEBUG("  Channel index: #%lld", channelIndex);

        varjo_Matrix extrinsics{};
        if (frame->dataFlags & varjo_DataFlag_Extrinsics) {
            extrinsics = varjo_GetCameraExtrinsics(session, frame->id, frame->frameNumber, channelIndex);
            CHECK_VARJO_ERR(m_session);
        }

        varjo_CameraIntrinsics intrinsics{};
        if (frame->dataFlags & varjo_DataFlag_Intrinsics) {
            intrinsics = varjo_GetCameraIntrinsics(session, frame->id, frame->frameNumber, channelIndex);
            CHECK_VARJO_ERR(m_session);
        }

        varjo_BufferId bufferId = varjo_InvalidId;
        if (frame->dataFlags & varjo_DataFlag_Buffer) {
            bufferId = varjo_GetBufferId(session, frame->id, frame->frameNumber, channelIndex);
            CHECK_VARJO_ERR(m_session);
        }

        if (bufferId == varjo_InvalidId) {
            LOG_WARNING("    (no buffer)");
            continue;
        }

        // Only handle buffer if the channel was requested
        if (stream.channels & c_channelFlags[channelIndex]) {
            Frame::Metadata frameMetadata;
            frameMetadata.streamFrame = *frame;
            frameMetadata.channelIndex = channelIndex;
            frameMetadata.timestamp = timestamp;
            frameMetadata.extrinsics = extrinsics;
            frameMetadata.intrinsics = intrinsics;
            handleBuffer(frameMetadata, bufferId, bufferFilenames[static_cast<size_t>(channelIndex)], snaphotRequested);
        }
    }
}

varjo_StreamId DataStreamer::startStreaming(varjo_StreamType type, varjo_TextureFormat format, varjo_ChannelFlag channels)
{
    varjo_StreamId streamId = varjo_InvalidId;

    // Fetch stream configs
    std::vector<varjo_StreamConfig> configs;
    configs.resize(varjo_GetDataStreamConfigCount(m_session));
    varjo_GetDataStreamConfigs(m_session, configs.data(), static_cast<int32_t>(configs.size()));
    CHECK_VARJO_ERR(m_session);

    // Only one channel in environment cubemap
    if (type == varjo_StreamType_EnvironmentCubemap) {
        channels &= varjo_ChannelFlag_First;
    }

    // Find suitable stream, start the frame stream, and provide callback for handling frames.
    for (const auto& conf : configs) {
        if (conf.streamType == type) {
            if ((conf.bufferType == varjo_BufferType_CPU) && ((conf.channelFlags & channels) == channels) && (conf.format == format)) {
                varjo_StartDataStream(m_session, conf.streamId, channels, dataStreamFrameCallback, this);
                if (CHECK_VARJO_ERR(m_session) == varjo_NoError) {
                    streamId = conf.streamId;
                }
                break;
            }
        }
    }

    return streamId;
}

bool DataStreamer::isDelayedBufferHandlingEnabled() const { return m_delayedBufferHandling; }

void DataStreamer::setDelayedBufferHandlingEnabled(bool enabled) { m_delayedBufferHandling = enabled; }

void DataStreamer::requestSnapshot(varjo_StreamType streamType, varjo_TextureFormat streamFormat)
{
    // Frame callback comes from different thread, lock streaming data
    std::lock_guard<std::recursive_mutex> streamLock(m_streamManagement.mutex);

    // Find out if we have running stream
    const auto it = std::find_if(
        m_streamManagement.streams.begin(), m_streamManagement.streams.end(), [streamType, streamFormat](const std::pair<varjo_StreamId, StreamData>& item) {
            return item.second.streamType == streamType && item.second.streamFormat == streamFormat;
        });

    if (it != m_streamManagement.streams.end()) {
        it->second.snapshotRequested = true;
    } else {
        LOG_WARNING("Failed to request snap shot. Not running stream: type=%lld, format=%lld", streamType, streamFormat);
    }
}

bool DataStreamer::convertToR8G8B8A(const varjo_BufferMetadata& buffer, const void* input, void* output, size_t outputRowStride)
{
    constexpr int32_t components = 4;
    if (outputRowStride == 0) {
        outputRowStride = buffer.width * components;
    }

    switch (buffer.format) {
        case varjo_TextureFormat_RGBA16_FLOAT: {
            // Background color for alpha blending
            const float rgbBackground[3] = {0.25f, 0.45f, 0.40f};

            // Convert half float to uint8
            const DirectX::PackedVector::HALF* halfSrc = reinterpret_cast<const DirectX::PackedVector::HALF*>(input);
            for (int32_t y = 0; y < buffer.height; y++) {
                uint8_t* line = static_cast<uint8_t*>(output) + outputRowStride * y;
                for (int32_t x = 0; x < buffer.width * components; x += components) {
                    // Streamed RGB values are in linear colorspace so we gamma correct them for screen here
                    constexpr float gamma = 1.0f / 2.2f;

                    // Read alpha
                    const float alpha = DirectX::PackedVector::XMConvertHalfToFloat(halfSrc[x + 3]);

                    // Read value, gamma correct, alpha blend to background color
                    for (int32_t c = 0; c < 3; c++) {
                        float value = powf(DirectX::PackedVector::XMConvertHalfToFloat(halfSrc[x + c]), gamma);
                        value = value * alpha + rgbBackground[c] * (1.0f - alpha);
                        line[x + c] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, 255.0f * value)));
                    }

                    // Write alpha
                    line[x + 3] = 255;
                    // line[x + 3] = static_cast<uint8_t>(max(0, min(255, 255.0 * alpha)));
                }

                halfSrc += buffer.rowStride / sizeof(DirectX::PackedVector::HALF);
            }
        } break;

        case varjo_TextureFormat_NV12: {
            // Convert YUV420 NV12 to RGBA8
            const uint8_t* bY = reinterpret_cast<const uint8_t*>(input);
            const uint8_t* bUV = bY + buffer.rowStride * buffer.height;

            for (int32_t y = 0; y < buffer.height; y++) {
                uint8_t* line = static_cast<uint8_t*>(output) + outputRowStride * y;
                size_t lineOffs = 0;
                for (int32_t x = 0; x < buffer.width; x++) {
                    const uint8_t Y = bY[x];

                    const auto uvX = x - (x & 1);
                    const uint8_t U = bUV[uvX + 0];
                    const uint8_t V = bUV[uvX + 1];

                    uint8_t R, G, B;
                    convertYUVtoRGB(Y, U, V, R, G, B);

                    // Write RGBA
                    line[lineOffs + 0] = R;
                    line[lineOffs + 1] = G;
                    line[lineOffs + 2] = B;
                    line[lineOffs + 3] = 255;
                    lineOffs += components;
                }
                bY += buffer.rowStride;
                if ((y & 1) == 1) {
                    bUV += buffer.rowStride;
                }
            }
        } break;

        case varjo_TextureFormat_Y8_UNORM: {
            // Convert Y8 to RGBA8
            const uint8_t* b = reinterpret_cast<const uint8_t*>(input);
            uint8_t* dst = static_cast<uint8_t*>(output);
            for (int32_t y = 0; y < buffer.height; y++) {
                for (int32_t x = 0; x < buffer.width; x++) {
                    const uint8_t Y = b[x];

                    // Write RGBA
                    *reinterpret_cast<uint32_t*>(dst) = c_convertY8ToRGBAMap[Y];
                    dst += sizeof(uint32_t);
                }
                b += buffer.rowStride;
                dst += outputRowStride - components * buffer.width;
            }
        } break;

        default: {
            CRITICAL("Unsupported pixel format: %d", static_cast<int>(buffer.format));
            return false;
        };
    }

    return true;
}

bool DataStreamer::convertDistortedYUVToRectifiedRGBA(const varjo_BufferMetadata& buffer, const uint8_t* input, const glm::ivec2& outputSize, uint8_t* output,
    const varjo_Matrix& extrinsics, const varjo_CameraIntrinsics& intrinsics, std::optional<const varjo_Matrix> projection)
{
    if (!(buffer.format == varjo_TextureFormat_NV12)) {
        CRITICAL("Unsupported pixel format: %d", static_cast<int>(buffer.format));
        return false;
    }

    const int bufferRowStride = buffer.rowStride;
    const int chromaResDivider = (buffer.format == varjo_TextureFormat_NV12) ? 2 : 1;

    const glm::ivec2 inputSize(buffer.width, buffer.height);
    const Undistorter undistorter(inputSize, outputSize, intrinsics, extrinsics, projection);

    // Calculate start address of Y- and UV-planes.
    const uint32_t uvOffs = (bufferRowStride * inputSize.y);
    const uint8_t* yStart = input;
    const uint8_t* uvStart = input + uvOffs;

    // Output pointer
    uint8_t* outPtr = output;
    size_t outOffs = 0;
    for (int y = 0; y < outputSize.y; ++y) {
        for (int x = 0; x < outputSize.x; ++x) {
            // Init to black which will be written in case the sample coordinate is invalid.
            uint8_t R = 0;
            uint8_t G = 0;
            uint8_t B = 0;

            // If the sample coord falls in within the buffer, sample from Y and UV planes and convert to RGB.
            const glm::ivec2 sampleCoord = undistorter.getSampleCoord(x, y);
            if (sampleCoord.x >= 0 && sampleCoord.x < inputSize.x && sampleCoord.y >= 0 && sampleCoord.y < inputSize.y) {
                const int uvX = sampleCoord.x - (sampleCoord.x & 1);

                int Y = yStart[sampleCoord.y * bufferRowStride + sampleCoord.x];
                int U = uvStart[sampleCoord.y / chromaResDivider * bufferRowStride + uvX + 0];
                int V = uvStart[sampleCoord.y / chromaResDivider * bufferRowStride + uvX + 1];

                convertYUVtoRGB(Y, U, V, R, G, B);
            }

            // Write out RGBA.
            outPtr[outOffs++] = R;
            outPtr[outOffs++] = G;
            outPtr[outOffs++] = B;
            outPtr[outOffs++] = 255;
        }
    }

    return true;
}

}  // namespace VarjoExamples

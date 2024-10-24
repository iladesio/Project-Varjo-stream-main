// Copyright 2019-2020 Varjo Technologies Oy. All rights reserved.

#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

#include <Varjo_datastream.h>

#include "Globals.hpp"

namespace VarjoExamples
{
//! Simple example class for testing Varjo data streaming
class DataStreamer
{
public:
    //! Frame data structure used with "onFrame" callback
    struct Frame {
        struct Metadata {
            varjo_StreamFrame streamFrame{};        //!< Stream frame information
            varjo_ChannelIndex channelIndex{0};     //!< Channel index
            varjo_Nanoseconds timestamp{0};         //!< Frame timestamp
            varjo_Matrix extrinsics{};              //!< Camera extrinsics (if available)
            varjo_CameraIntrinsics intrinsics{};    //!< Camera frame intrinsics (if available)
            varjo_BufferMetadata bufferMetadata{};  //!< Buffer metadata
        };
        Metadata metadata{};        //!< Frame metadata
        std::vector<uint8_t> data;  //!< Buffer data
    };

    //! Construct data streamer
    DataStreamer(varjo_Session* session, const std::function<void(const Frame&)>& onFrameCallback);

    //! Destruct data streamer. Cleans up running data streams.
    ~DataStreamer();

    // Disable copy, move and assign
    DataStreamer(const DataStreamer& other) = delete;
    DataStreamer(const DataStreamer&& other) = delete;
    DataStreamer& operator=(const DataStreamer& other) = delete;
    DataStreamer& operator=(const DataStreamer&& other) = delete;

    // Returns configuration for a stream with given type
    std::optional<varjo_StreamConfig> getConfig(varjo_StreamType streamType) const;

    // Returns preferred texture format for given type
    varjo_TextureFormat getFormat(varjo_StreamType streamType) const;

    //! Start data streaming
    void startDataStream(varjo_StreamType streamType, varjo_TextureFormat streamFormat, varjo_ChannelFlag channels);

    //! Stop data streaming
    void stopDataStream(varjo_StreamType streamType, varjo_TextureFormat streamFormat);

    //! Is streaming
    bool isStreaming() const;

    //! Is streaming given type with any format
    bool isStreaming(varjo_StreamType streamType) const;

    //! Is streaming given type and format
    bool isStreaming(varjo_StreamType streamType, varjo_TextureFormat streamFormat) const;

    //! Return if streaming and if so, get streaming channels
    bool isStreaming(varjo_StreamType streamType, varjo_TextureFormat streamFormat, varjo_ChannelFlag& outChannels) const;

    //! Handle delayed data stream buffers. Optionally ignore frames
    void handleDelayedBuffers(bool ignore = false);

    //! Print out currently available data stream configs
    void printStreamConfigs() const;

    //! Is delayed bufferhandling currently enabled
    bool isDelayedBufferHandlingEnabled() const;

    //! Set delayed bufferhandling enabled
    void setDelayedBufferHandlingEnabled(bool enabled);

    //! Return status line
    std::string getStatusLine() const { return isStreaming() ? (m_statusLine.empty() ? "Not streaming." : m_statusLine) : "Not streaming."; }

    //! Requests making snapshot for next frame
    void requestSnapshot(varjo_StreamType streamType, varjo_TextureFormat streamFormat);

    //! Helper function for converting input buffer to R8G8B8A8 color format
    static bool convertToR8G8B8A(const varjo_BufferMetadata& buffer, const void* input, void* output, size_t outputRowStride = 0);

    //! Helper function for converting distorted YUV input buffer to rectified RGBA output buffer
    static bool convertDistortedYUVToRectifiedRGBA(const varjo_BufferMetadata& buffer, const uint8_t* input, const glm::ivec2& outputSize, uint8_t* output,
        const varjo_Matrix& extrinsics, const varjo_CameraIntrinsics& intrinsics, std::optional<const varjo_Matrix> projection);

private:
    //! Static data stream frame callback function
    static void dataStreamFrameCallback(const varjo_StreamFrame* frame, varjo_Session* session, void* userData);

    //! Called from data stream frame callback
    void onDataStreamFrame(const varjo_StreamFrame* frame, varjo_Session* session);

    //! Handle frame buffer
    void handleBuffer(const Frame::Metadata& frameMetadata, varjo_BufferId bufferId, const std::string& baseName, bool takeSnapshot);

    //! Store buffer contents to file
    void storeBuffer(const Frame::Metadata& frameMetadata, varjo_BufferId bufferId, void* cpuData, const std::string& baseName, bool takeSnapshot);

    //! Find data stream of given type and texture format and start it
    varjo_StreamId startStreaming(varjo_StreamType streamType, varjo_TextureFormat streamFormat, varjo_ChannelFlag channels);

    //! Get streaming ID
    std::pair<varjo_StreamId, varjo_ChannelFlag> getStreamingIdAndChannel(varjo_StreamType streamType, varjo_TextureFormat streamFormat) const;

private:
    //! Delayed buffer info structure
    struct DelayedBuffer {
        Frame::Metadata frame{};                   //!< Frame metadata
        std::string baseName;                      //!< Base filename
        varjo_BufferId bufferId{varjo_InvalidId};  //!< Varjo buffer identifier
        void* cpuBuffer{nullptr};                  //!< Pointer to CPU buffer data
        bool takeSnapshot{false};                  //!< Flag indicating whether stream snapshot should be created
    };

    //! Internal frame data
    struct FrameData {
        Frame frame;
        int64_t frameCount{0};
    };

    //! Internal stream data
    struct StreamData {
        varjo_StreamId streamId{varjo_InvalidId};                       //!< Stream ID
        varjo_StreamType streamType{0};                                 //!< Stream type
        varjo_TextureFormat streamFormat{varjo_TextureFormat_INVALID};  //!< Stream format
        varjo_ChannelFlag channels{varjo_ChannelFlag_None};             //!< Channels
        bool snapshotRequested{true};                                   //!< Flag indicating whether stream snapshot should be created
        std::map<varjo_ChannelIndex, FrameData> frameData;              //!< Frame data for each channel
    };

    //! Struct for thread safe stream management
    struct StreamManagement {
        mutable std::recursive_mutex mutex;                      //!< Mutex for locking streamer data
        std::atomic_bool running{false};                         //!< If true streaming is running
        std::unordered_map<varjo_StreamId, StreamData> streams;  //!< Set of running streams
        std::vector<DelayedBuffer> delayedBuffers;               //!< List of delayed buffers
    };

    varjo_Session* m_session{nullptr};                          //!< Varjo session
    const std::function<void(const Frame&)> m_onFrameCallback;  //!< Frame callback function
    std::atomic_bool m_delayedBufferHandling{false};            //!< Flag for delayed buffer handling
    StreamManagement m_streamManagement;                        //!< Stream management data
    std::string m_statusLine;                                   //!< Streaming status line

    //! Stream statistics
    struct {
        uint64_t streamCount{0};                                      //!< Stream count
        uint64_t frameCount{0};                                       //!< Frame count
        std::chrono::high_resolution_clock::time_point reportTime{};  //!< Last report time
    } m_stats;
};

}  // namespace VarjoExamples

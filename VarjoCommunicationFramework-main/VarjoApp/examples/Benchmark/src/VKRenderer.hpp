#pragma once

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_TYPESAFE_CONVERSION
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.hpp>

#include "IRenderer.hpp"

#include <vector>
#include <array>
#include <map>
#include <set>

class VKGeometry;

class VKBufferBase
{
protected:
    VKBufferBase() = default;
    VKBufferBase(vk::Device vkDevice, vk::Queue vkQueue, vk::CommandPool transientCommandPool);

    vk::UniqueBuffer createBuffer(const vk::PhysicalDeviceMemoryProperties& memoryProperties, std::size_t dataSize, vk::BufferUsageFlags bufferUsage,
        vk::MemoryPropertyFlags requiredMemoryProperties, vk::MemoryPropertyFlags preferredMemoryProperties,
        vk::MemoryPropertyFlags& outFoundMemoryTypeProperties, vk::UniqueDeviceMemory& outDeviceMemory);
    void createStagingBuffer(const vk::PhysicalDeviceMemoryProperties& memoryProperties, std::size_t transferDataSize);

    void transferMemoryFenceAsync(
        const void* data, std::size_t size, vk::DeviceMemory deviceMemory, vk::Buffer destinationBuffer, vk::CommandBuffer cmdBuffer, vk::Fence fence);
    void transferMemoryFenceSync(const void* data, std::size_t size, vk::DeviceMemory deviceMemory, vk::Buffer destinationBuffer, vk::Fence fence);

private:
    vk::UniqueCommandBuffer allocateTransientCommandBuffer();
    void transferMemory(const void* data, std::size_t size, vk::DeviceMemory deviceMemory, vk::Buffer destinationBuffer);

protected:
    vk::Device m_vkDevice;
    vk::Queue m_vkQueue;
    vk::CommandPool m_transientCommandPool;

private:
    bool m_stagingBufferHostCoherent;
    vk::UniqueDeviceMemory m_stagingDeviceMemory;
    vk::UniqueBuffer m_stagingBuffer;
};

class VKInstanceBuffer final : private VKBufferBase
{
public:
    VKInstanceBuffer() = default;
    VKInstanceBuffer(vk::Device vkDevice, vk::Queue vkQueue, vk::CommandPool transientCommandPool);

    void transferInstanceData(
        vk::PhysicalDevice vkPhysicalDevice, const std::vector<IRenderer::ObjectRenderData>& objectRenderData, vk::CommandBuffer cmdBuffer, vk::Fence fence);
    void bind(vk::CommandBuffer& commandBuffer, uint32_t binding, vk::DeviceSize offset);

private:
    std::size_t m_size = 0;
    vk::UniqueDeviceMemory m_deviceMemory;
    vk::UniqueBuffer m_buffer;
};

class VKOcclusionMeshGeometry final : private VKBufferBase
{
public:
    VKOcclusionMeshGeometry() = default;
    VKOcclusionMeshGeometry(
        vk::PhysicalDevice vkPhysicalDevice, vk::Device vkDevice, vk::Queue vkQueue, vk::CommandPool transientCommandPool, varjo_Mesh2Df* occlusionMesh);

    void bind(vk::CommandBuffer cmdBuffer, uint32_t binding);

    uint32_t getVertexCount() const { return m_vertexCount; }

    operator bool() const { return m_vertexCount != 0; }

private:
    uint32_t m_vertexCount = 0;
    std::size_t m_stagingDataSize;
    std::size_t m_vertexDataSize;
    vk::UniqueDeviceMemory m_vertexDeviceMemory;
    vk::UniqueBuffer m_vertexBuffer;
};

class VKRenderer final : public IRenderer
{
public:
    VKRenderer(varjo_Session* session, const RendererSettings& rendererSettings);
    ~VKRenderer() override;

    std::shared_ptr<Geometry> createGeometry(uint32_t vertexCount, uint32_t indexCount) override;
    std::shared_ptr<RenderTexture> createColorTexture(int32_t width, int32_t height, varjo_Texture colorTexture) override;
    std::shared_ptr<RenderTexture> createDepthTexture(int32_t width, int32_t height, varjo_Texture depthTexture) override;
    std::shared_ptr<RenderTexture> createVelocityTexture(int32_t width, int32_t height, varjo_Texture velocityTexture) override;
    bool isVrsSupported() const override;
    void finishRendering() override;

    VkDevice getDevice() const;
    void recreateOcclusionMesh(uint32_t viewIndex) override;

protected:
    bool initVarjo() override;
    void createSwapchains() override;
    varjo_SwapChain* createSwapChain(varjo_SwapChainConfig2& swapchainConfig) override;
    void bindRenderTarget(const RenderTargetTextures& renderTarget) override;
    void unbindRenderTarget() override;
    void clearRenderTarget(const RenderTargetTextures& renderTarget, float r, float g, float b, float a) override;
    void freeCurrentRenderTarget() override;
    void useGeometry(const std::shared_ptr<Geometry>& geometry) override;
    void setupCamera(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;
    void setViewport(const varjo_Viewport& viewport) override;
    void updateVrsMap(const varjo_Viewport& viewport) override;
    void uploadInstanceBuffer(const std::vector<std::vector<ObjectRenderData>>& matrices) override;
    void renderOcclusionMesh() override;
    void drawGrid() override;
    void drawObjects(std::size_t objectsIndex) override;
    void drawMirrorWindow() override;
    void advance() override;
    void preRenderFrame() override;
    void postRenderFrame() override;
    varjo_ClipRange getClipRange() const override;

private:
    bool initialize(varjo_Session* session);
    bool createInstance(varjo_Session* session);
    bool createDevice(varjo_Session* session);
    bool findQueueFamilies(vk::PhysicalDevice physicalDevice);
    bool createFrameResources();

    int currentFrameIndex() const { return static_cast<int>(frameNumber % 3); }

    void createRenderPass();
    bool createGraphicsPipelines();
    bool createGridPipeline();
    bool createScenePipeline();
    bool createStencilPipeline();

    void createOcclusionMeshGeometry(uint32_t viewIndex);

    void setViewportCommands(vk::CommandBuffer cmdBuffer);

    struct ShaderPushConstants {
        glm::mat4 viewMatrix;
        glm::mat4 projectionMatrix;
        glm::vec2 viewportSize;
    };

    enum : std::size_t {
        cmdTransfer,
        cmdDraw,
        numCommands,
    };

    enum : std::size_t {
        subpassStencil,
        subpassColor,
        numSubpasses,
    };

    static constexpr std::size_t numFrames = 3;

    bool m_hasStencil;
    vk::Format m_colorFormat;
    vk::Format m_depthFormat;
    vk::Format m_velocityFormat;

    vk::DynamicLoader vkLoader;
    vk::UniqueInstance vkInstance;
#ifdef _DEBUG
    vk::UniqueDebugUtilsMessengerEXT vkDebugUtilsMessenger;
#endif
    vk::PhysicalDevice vkPhysicalDevice;
    vk::UniqueDevice vkDevice;
    vk::Queue graphicsQueue;
    vk::UniqueCommandPool commandPool[numFrames];
    vk::UniqueCommandBuffer cmdBuffers[numFrames][numCommands];
    vk::UniqueCommandBuffer subpassCmdBuffers[numFrames][numSubpasses];
    vk::UniqueFence fences[numFrames][numCommands];
    int64_t frameNumber = 0;
    uint32_t graphicsQueueFamily = -1;
    vk::UniqueCommandPool transientCommandPool;

    RenderTargetTextures m_currentRenderTarget;
    vk::Framebuffer m_currentFramebuffer;
    std::map<std::set<RenderTexture*>, vk::UniqueFramebuffer> m_framebuffers;

    VKInstanceBuffer m_instanceBuffer;
    std::vector<std::pair<std::size_t, std::size_t>> m_instancedDrawsOffsetCount;

    vk::UniqueRenderPass renderPass;

    vk::UniqueShaderModule gridFragmentShader;
    vk::UniqueShaderModule gridVertexShader;
    vk::UniquePipelineLayout gridPipelineLayout;
    vk::UniquePipeline gridPipeline;

    vk::UniqueShaderModule sceneFragmentShader;
    vk::UniqueShaderModule sceneVertexShader;
    vk::UniquePipelineLayout scenePipelineLayout;
    vk::UniquePipeline scenePipeline;

    vk::UniqueShaderModule stencilFragmentShader;
    vk::UniqueShaderModule stencilVertexShader;
    vk::UniquePipelineLayout stencilPipelineLayout;
    vk::UniquePipeline stencilPipeline;

    std::shared_ptr<VKGeometry> m_currentGeometry;
    ShaderPushConstants m_pushConstants;
    vk::Rect2D m_currentViewport;
    std::array<float, 4> m_clearColor;

    VKOcclusionMeshGeometry m_occlusionMeshGeometry[2];
};

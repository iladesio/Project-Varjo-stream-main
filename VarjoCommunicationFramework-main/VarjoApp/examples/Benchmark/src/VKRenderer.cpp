#include "VKRenderer.hpp"
#include "VkShaders.hpp"
#include <Varjo.h>
#include <Varjo_layers.h>
#include <Varjo_vk.h>
#include "Geometry.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace
{
uint32_t getBestMemoryType(const vk::PhysicalDeviceMemoryProperties& memoryProperties, uint32_t memoryTypeBits,
    vk::MemoryPropertyFlags requiredMemoryProperties, vk::MemoryPropertyFlags preferredMemoryProperties, vk::MemoryPropertyFlags& outFoundMemoryTypeProperties)
{
    const vk::MemoryPropertyFlags idealMemoryProperties = requiredMemoryProperties | preferredMemoryProperties;

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((memoryTypeBits & (1 << i)) != 0 && (memoryProperties.memoryTypes[i].propertyFlags & idealMemoryProperties) == idealMemoryProperties) {
            outFoundMemoryTypeProperties = memoryProperties.memoryTypes[i].propertyFlags;
            return i;
        }
    }

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((memoryTypeBits & (1 << i)) != 0 && (memoryProperties.memoryTypes[i].propertyFlags & requiredMemoryProperties) == requiredMemoryProperties) {
            outFoundMemoryTypeProperties = memoryProperties.memoryTypes[i].propertyFlags;
            return i;
        }
    }

    // Should never happen
    std::printf("Couldn't find appropriate memory type for buffer\n");
    abort();
    return 0;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    (void)pUserData;

    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: std::printf("[VERBOSE]"); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: std::printf("[INFO]"); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: std::printf("[WARNING]"); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: std::printf("[ERROR]"); break;
        default: std::printf("[UNKNOWN]"); break;
    }

    std::printf(" ");

    if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        std::printf("GENERAL ");
    }

    if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        std::printf("VALIDATION ");
    }

    if (messageTypes & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        std::printf("PERFORMANCE ");
    }

    if (pCallbackData->pMessageIdName) {
        std::printf("%s: %s\n", pCallbackData->pMessageIdName, pCallbackData->pMessage);
    } else {
        std::printf("%s\n", pCallbackData->pMessage);
    }

    return VK_FALSE;
}
}  // namespace

class VKColorRenderTexture final : public RenderTexture
{
public:
    VKColorRenderTexture(vk::Device vkDevice, int32_t width, int32_t height, VkImage texture, vk::Format format)
        : RenderTexture(width, height)
        , m_texture(texture)
    {
        m_imageView = vkDevice.createImageViewUnique(vk::ImageViewCreateInfo()
                                                         .setImage(texture)
                                                         .setViewType(vk::ImageViewType::e2D)
                                                         .setFormat(format)
                                                         .setSubresourceRange(vk::ImageSubresourceRange()
                                                                                  .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                                  .setBaseMipLevel(0)
                                                                                  .setLevelCount(1)
                                                                                  .setBaseArrayLayer(0)
                                                                                  .setLayerCount(1)));
    }

    varjo_Texture texture() const override { return varjo_FromVkTexture(m_texture); }

    vk::Image getImage() const { return m_texture; }
    vk::ImageView getImageView() const { return m_imageView.get(); }

private:
    vk::Image m_texture;
    vk::UniqueImageView m_imageView;
};

class VKDepthRenderTexture final : public RenderTexture
{
public:
    VKDepthRenderTexture(
        vk::PhysicalDevice vkPhysicalDevice, vk::Device vkDevice, int32_t width, int32_t height, VkImage texture, vk::Format format, bool hasStencil)
        : RenderTexture(width, height)
        , m_texture(texture)
    {
        if (!m_texture) {
            m_ownedImage = vkDevice.createImageUnique(vk::ImageCreateInfo()
                                                          .setImageType(vk::ImageType::e2D)
                                                          .setFormat(format)
                                                          .setExtent(vk::Extent3D(width, height, 1))
                                                          .setMipLevels(1)
                                                          .setArrayLayers(1)
                                                          .setSamples(vk::SampleCountFlagBits::e1)
                                                          .setTiling(vk::ImageTiling::eOptimal)
                                                          .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
                                                          .setSharingMode(vk::SharingMode::eExclusive)
                                                          .setInitialLayout(vk::ImageLayout::eUndefined));
            m_texture = m_ownedImage.get();

            const auto memoryProperties = vkPhysicalDevice.getMemoryProperties();
            const auto memoryRequirements = vkDevice.getImageMemoryRequirements(m_texture);
            vk::MemoryPropertyFlags imageMemoryProperties;
            const uint32_t memoryType =
                getBestMemoryType(memoryProperties, memoryRequirements.memoryTypeBits, {}, vk::MemoryPropertyFlagBits::eDeviceLocal, imageMemoryProperties);

            const auto allocateInfo = vk::MemoryAllocateInfo().setAllocationSize(memoryRequirements.size).setMemoryTypeIndex(memoryType);
            m_ownedDeviceMemory = vkDevice.allocateMemoryUnique(allocateInfo);
            vkDevice.bindImageMemory(m_texture, m_ownedDeviceMemory.get(), 0);
        }

        m_imageView = vkDevice.createImageViewUnique(
            vk::ImageViewCreateInfo()
                .setImage(m_texture)
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(format)
                .setSubresourceRange(
                    vk::ImageSubresourceRange()
                        .setAspectMask(hasStencil ? vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil : vk::ImageAspectFlagBits::eDepth)
                        .setBaseMipLevel(0)
                        .setLevelCount(1)
                        .setBaseArrayLayer(0)
                        .setLayerCount(1)));
    }

    varjo_Texture texture() const override { return varjo_FromVkTexture(m_texture); }

    vk::Image getImage() const { return m_texture; }
    vk::ImageView getImageView() const { return m_imageView.get(); }

private:
    vk::Image m_texture;
    vk::UniqueDeviceMemory m_ownedDeviceMemory;
    vk::UniqueImage m_ownedImage;
    vk::UniqueImageView m_imageView;
};

class VKGeometry final : public Geometry, private VKBufferBase
{
public:
    VKGeometry(vk::PhysicalDevice vkPhysicalDevice, vk::Device vkDevice, vk::Queue vkQueue, vk::CommandPool transientCommandPool, uint32_t vertexCount,
        uint32_t indexCount)
        : Geometry(vertexCount, indexCount)
        , VKBufferBase(vkDevice, vkQueue, transientCommandPool)
    {
        const auto memoryProperties = vkPhysicalDevice.getMemoryProperties();

        m_vertexDataSize = sizeof(Geometry::Vertex) * vertexCount;
        m_indexDataSize = sizeof(uint32_t) * indexCount;
        m_stagingDataSize = std::max(m_vertexDataSize, m_indexDataSize);

        createStagingBuffer(memoryProperties, m_stagingDataSize);

        vk::MemoryPropertyFlags bufferMemoryProperties;
        m_vertexBuffer = createBuffer(memoryProperties, m_vertexDataSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, {},
            vk::MemoryPropertyFlagBits::eDeviceLocal, bufferMemoryProperties, m_vertexDeviceMemory);

        m_indexBuffer = createBuffer(memoryProperties, m_indexDataSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, {},
            vk::MemoryPropertyFlagBits::eDeviceLocal, bufferMemoryProperties, m_indexDeviceMemory);

        m_fence = vkDevice.createFenceUnique(vk::FenceCreateInfo());
    }

    void updateVertexBuffer(void* data) override
    {
        transferMemoryFenceSync(data, m_vertexDataSize, m_vertexDeviceMemory.get(), m_vertexBuffer.get(), m_fence.get());
    }

    void updateIndexBuffer(void* data) override
    {
        transferMemoryFenceSync(data, m_indexDataSize, m_indexDeviceMemory.get(), m_indexBuffer.get(), m_fence.get());
    }

    void bind(vk::CommandBuffer cmdBuffer, uint32_t binding)
    {
        cmdBuffer.bindIndexBuffer(m_indexBuffer.get(), 0, vk::IndexType::eUint32);

        vk::DeviceSize offsets[1] = {0};
        cmdBuffer.bindVertexBuffers(binding, 1, &m_vertexBuffer.get(), offsets);
    }

private:
    std::size_t m_stagingDataSize;
    std::size_t m_vertexDataSize;
    std::size_t m_indexDataSize;
    vk::UniqueDeviceMemory m_vertexDeviceMemory;
    vk::UniqueBuffer m_vertexBuffer;
    vk::UniqueDeviceMemory m_indexDeviceMemory;
    vk::UniqueBuffer m_indexBuffer;
    vk::UniqueFence m_fence;
};

VKBufferBase::VKBufferBase(vk::Device vkDevice, vk::Queue vkQueue, vk::CommandPool transientCommandPool)
    : m_vkDevice(vkDevice)
    , m_vkQueue(vkQueue)
    , m_transientCommandPool(transientCommandPool)
{
}

vk::UniqueBuffer VKBufferBase::createBuffer(const vk::PhysicalDeviceMemoryProperties& memoryProperties, std::size_t dataSize, vk::BufferUsageFlags bufferUsage,
    vk::MemoryPropertyFlags requiredMemoryProperties, vk::MemoryPropertyFlags preferredMemoryProperties, vk::MemoryPropertyFlags& outFoundMemoryTypeProperties,
    vk::UniqueDeviceMemory& outDeviceMemory)
{
    const auto createInfo = vk::BufferCreateInfo().setSize(dataSize).setUsage(bufferUsage).setSharingMode(vk::SharingMode::eExclusive);
    vk::UniqueBuffer buffer = m_vkDevice.createBufferUnique(createInfo);

    const auto memoryRequirements = m_vkDevice.getBufferMemoryRequirements(buffer.get());
    const uint32_t memoryType = getBestMemoryType(
        memoryProperties, memoryRequirements.memoryTypeBits, requiredMemoryProperties, preferredMemoryProperties, outFoundMemoryTypeProperties);

    // TODO we shouldn't call vkAllocateMemory for each buffer, instead we should have one big memory chunk and suballocate ourselves. But we won't be
    // creating any buffers dynamically during rendering, so it's probably fine like this.
    const auto allocateInfo = vk::MemoryAllocateInfo().setAllocationSize(memoryRequirements.size).setMemoryTypeIndex(memoryType);
    outDeviceMemory = m_vkDevice.allocateMemoryUnique(allocateInfo);
    m_vkDevice.bindBufferMemory(buffer.get(), outDeviceMemory.get(), 0);

    return buffer;
}

void VKBufferBase::createStagingBuffer(const vk::PhysicalDeviceMemoryProperties& memoryProperties, std::size_t transferDataSize)
{
    m_stagingBuffer.reset();
    m_stagingDeviceMemory.reset();

    vk::MemoryPropertyFlags bufferMemoryProperties;
    m_stagingBuffer = createBuffer(memoryProperties, transferDataSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible,
        vk::MemoryPropertyFlagBits::eHostCoherent, bufferMemoryProperties, m_stagingDeviceMemory);
    m_stagingBufferHostCoherent = static_cast<bool>(bufferMemoryProperties & vk::MemoryPropertyFlagBits::eHostCoherent);
}

void VKBufferBase::transferMemoryFenceAsync(
    const void* data, std::size_t size, vk::DeviceMemory deviceMemory, vk::Buffer destinationBuffer, vk::CommandBuffer cmdBuffer, vk::Fence fence)
{
    transferMemory(data, size, deviceMemory, destinationBuffer);

    cmdBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    cmdBuffer.copyBuffer(m_stagingBuffer.get(), destinationBuffer, vk::BufferCopy().setSrcOffset(0).setDstOffset(0).setSize(size));
    cmdBuffer.end();

    const auto submitInfo = vk::CommandBufferSubmitInfoKHR().setCommandBuffer(cmdBuffer);
    m_vkQueue.submit2KHR(vk::SubmitInfo2KHR().setCommandBufferInfos(submitInfo), fence);
}

void VKBufferBase::transferMemoryFenceSync(const void* data, std::size_t size, vk::DeviceMemory deviceMemory, vk::Buffer destinationBuffer, vk::Fence fence)
{
    vk::UniqueCommandBuffer commandBuffer = allocateTransientCommandBuffer();
    transferMemoryFenceAsync(data, size, deviceMemory, destinationBuffer, commandBuffer.get(), fence);

    if (m_vkDevice.waitForFences(fence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) {
        std::printf("Error waiting for fence\n");
        abort();
    }

    m_vkDevice.resetFences(fence);
}

vk::UniqueCommandBuffer VKBufferBase::allocateTransientCommandBuffer()
{
    const auto commandBufferAllocateInfo =
        vk::CommandBufferAllocateInfo().setCommandPool(m_transientCommandPool).setLevel(vk::CommandBufferLevel::ePrimary).setCommandBufferCount(1);
    return std::move(m_vkDevice.allocateCommandBuffersUnique(commandBufferAllocateInfo)[0]);
}

void VKBufferBase::transferMemory(const void* data, std::size_t size, vk::DeviceMemory deviceMemory, vk::Buffer destinationBuffer)
{
    const auto memoryRange = vk::MappedMemoryRange().setMemory(m_stagingDeviceMemory.get()).setOffset(0).setSize(size);

    void* gpuData;
    if (m_vkDevice.mapMemory(m_stagingDeviceMemory.get(), 0, size, {}, &gpuData) != vk::Result::eSuccess) {
        std::printf("Error mapping GPU memory\n");
        abort();
    }

    if (!m_stagingBufferHostCoherent) {
        m_vkDevice.invalidateMappedMemoryRanges(memoryRange);
    }

    std::memcpy(gpuData, data, size);

    if (!m_stagingBufferHostCoherent) {
        m_vkDevice.flushMappedMemoryRanges(memoryRange);
    }
    m_vkDevice.unmapMemory(m_stagingDeviceMemory.get());
}

VKInstanceBuffer::VKInstanceBuffer(vk::Device vkDevice, vk::Queue vkQueue, vk::CommandPool transientCommandPool)
    : VKBufferBase(vkDevice, vkQueue, transientCommandPool)
{
}

void VKInstanceBuffer::transferInstanceData(
    vk::PhysicalDevice vkPhysicalDevice, const std::vector<IRenderer::ObjectRenderData>& objectRenderData, vk::CommandBuffer cmdBuffer, vk::Fence fence)
{
    std::size_t dataSize = objectRenderData.size() * sizeof(objectRenderData[0]);

    if (m_size < dataSize) {
        m_size = dataSize;
        const auto memoryProperties = vkPhysicalDevice.getMemoryProperties();

        createStagingBuffer(memoryProperties, m_size);

        m_buffer.reset();
        m_deviceMemory.reset();

        vk::MemoryPropertyFlags bufferMemoryProperties;
        m_buffer = createBuffer(memoryProperties, m_size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, {},
            vk::MemoryPropertyFlagBits::eDeviceLocal, bufferMemoryProperties, m_deviceMemory);
    }

    const auto bufferMemoryBarrier = vk::BufferMemoryBarrier2KHR()
                                         .setSrcStageMask(vk::PipelineStageFlagBits2KHR::eTransfer)
                                         .setSrcAccessMask(vk::AccessFlagBits2KHR::eTransferWrite)
                                         .setDstStageMask(vk::PipelineStageFlagBits2KHR::eVertexAttributeInput)
                                         .setDstAccessMask(vk::AccessFlagBits2KHR::eVertexAttributeRead)
                                         .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                         .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                         .setBuffer(m_buffer.get())
                                         .setOffset(0)
                                         .setSize(VK_WHOLE_SIZE);
    const auto depencencyInfo = vk::DependencyInfoKHR().setBufferMemoryBarriers(bufferMemoryBarrier);
    transferMemoryFenceAsync(objectRenderData.data(), dataSize, m_deviceMemory.get(), m_buffer.get(), cmdBuffer, fence);
}

void VKInstanceBuffer::bind(vk::CommandBuffer& cmdBuffer, uint32_t binding, vk::DeviceSize offset)
{
    vk::DeviceSize offsets[1] = {offset};
    cmdBuffer.bindVertexBuffers(binding, 1, &m_buffer.get(), offsets);
}

VKOcclusionMeshGeometry::VKOcclusionMeshGeometry(
    vk::PhysicalDevice vkPhysicalDevice, vk::Device vkDevice, vk::Queue vkQueue, vk::CommandPool transientCommandPool, varjo_Mesh2Df* occlusionMesh)
    : VKBufferBase(vkDevice, vkQueue, transientCommandPool)
    , m_vertexCount(occlusionMesh->vertexCount)
{
    const auto memoryProperties = vkPhysicalDevice.getMemoryProperties();

    m_vertexDataSize = sizeof(varjo_Vector2Df) * occlusionMesh->vertexCount;

    if (m_vertexDataSize) {
        createStagingBuffer(memoryProperties, m_vertexDataSize);

        vk::MemoryPropertyFlags bufferMemoryProperties;
        m_vertexBuffer = createBuffer(memoryProperties, m_vertexDataSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, {},
            vk::MemoryPropertyFlagBits::eDeviceLocal, bufferMemoryProperties, m_vertexDeviceMemory);

        auto fence = vkDevice.createFenceUnique(vk::FenceCreateInfo());
        transferMemoryFenceSync(occlusionMesh->vertices, m_vertexDataSize, m_vertexDeviceMemory.get(), m_vertexBuffer.get(), fence.get());
    }
}

void VKOcclusionMeshGeometry::bind(vk::CommandBuffer cmdBuffer, uint32_t binding)
{
    vk::DeviceSize offsets[1] = {0};
    cmdBuffer.bindVertexBuffers(binding, 1, &m_vertexBuffer.get(), offsets);
}

bool VKRenderer::initialize(varjo_Session* session)
{
    m_colorFormat = m_settings.noSrgb() ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8A8Srgb;
    switch (m_settings.depthFormat()) {
        case varjo_DepthTextureFormat_D32_FLOAT: {
            m_hasStencil = false;
            m_depthFormat = vk::Format::eD32Sfloat;
            break;
        }
        case varjo_DepthTextureFormat_D24_UNORM_S8_UINT: {
            m_hasStencil = true;
            m_depthFormat = vk::Format::eD24UnormS8Uint;
            break;
        }
        case varjo_DepthTextureFormat_D32_FLOAT_S8_UINT: {
            m_hasStencil = true;
            m_depthFormat = vk::Format::eD32SfloatS8Uint;
            break;
        }
        default: {
            printf("ERROR: Unsupported depth stencil texture format: %d\n", static_cast<int>(m_settings.depthFormat()));
            abort();
            break;
        }
    }

    m_velocityFormat = vk::Format::eR8G8B8A8Uint;

    const PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = vkLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

#define CHECK(a)      \
    if (!(a)) {       \
        return false; \
    }

    CHECK(createInstance(session));
    CHECK(createDevice(session));
    createRenderPass();
    createGraphicsPipelines();
    CHECK(createFrameResources());

    for (uint32_t viewIndex = 0; viewIndex < 2; viewIndex++) {
        createOcclusionMeshGeometry(viewIndex);
    }
    return true;
}

bool VKRenderer::createInstance(varjo_Session* session)
{
    int instanceExtensionCount = 0;
    varjo_GetInstanceExtensionsVk(session, &instanceExtensionCount, nullptr);
    std::vector<const char*> enabledInstanceExtensions(instanceExtensionCount);
    varjo_GetInstanceExtensionsVk(session, &instanceExtensionCount, enabledInstanceExtensions.data());

#ifdef _DEBUG
    enabledInstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    printf("Enabled Vulkan instance extensions:\n");
    for (const auto& ext : enabledInstanceExtensions) {
        printf("    %s\n", ext);
    }

    const std::vector<const char*> enabledLayers = {
#ifdef _DEBUG
        "VK_LAYER_KHRONOS_validation"
#endif
    };

    const auto applicationInfo = vk::ApplicationInfo().setApiVersion(VK_MAKE_VERSION(1, 0, 0));
    const vk::InstanceCreateInfo info = vk::InstanceCreateInfo()
                                            .setPEnabledLayerNames(enabledLayers)
                                            .setPEnabledExtensionNames(enabledInstanceExtensions)
                                            .setPApplicationInfo(&applicationInfo);
    vkInstance = vk::createInstanceUnique(info);
    // if (res != vk::Result::eSuccess) {
    //    fprintf(stderr, "Failed to create a Vulkan instance, error code = %x", res);
    //    return false;
    //}

    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkInstance.get());

#ifdef _DEBUG
    vkDebugUtilsMessenger = vkInstance->createDebugUtilsMessengerEXTUnique(
        vk::DebugUtilsMessengerCreateInfoEXT()
            .setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
            .setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)
            .setPfnUserCallback(debugUtilsMessengerCallback));
#endif

    return true;
}

bool VKRenderer::createDevice(varjo_Session* session)
{
    vkPhysicalDevice = varjo_GetPhysicalDeviceVk(session, vkInstance.get());
    CHECK(findQueueFamilies(vkPhysicalDevice));

    const float priority = 1.f;
    std::vector<vk::DeviceQueueCreateInfo> queueDesc;
    for (const int queueFamily : {graphicsQueueFamily}) {
        queueDesc.push_back(vk::DeviceQueueCreateInfo().setQueueFamilyIndex(queueFamily).setQueueCount(1).setPQueuePriorities(&priority));
    }

    const auto deviceFeatures = vk::PhysicalDeviceFeatures();

    auto synchronization2Features = vk::PhysicalDeviceSynchronization2Features().setSynchronization2(VK_TRUE);

    int32_t deviceExtensionsCount = 0;
    varjo_GetDeviceExtensionsVk(session, &deviceExtensionsCount, nullptr);
    std::vector<const char*> extVec(deviceExtensionsCount);
    varjo_GetDeviceExtensionsVk(session, &deviceExtensionsCount, extVec.data());

    extVec.push_back(VK_KHR_MAINTENANCE_1_EXTENSION_NAME);
    extVec.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);

    printf("Enabled Vulkan device extensions:\n");
    for (const auto& ext : extVec) {
        printf("    %s\n", ext);
    }

    std::vector<char*> layerVec;

    const auto deviceDesc = vk::DeviceCreateInfo()
                                .setQueueCreateInfos(queueDesc)
                                .setPEnabledFeatures(&deviceFeatures)
                                .setPEnabledExtensionNames(extVec)
                                .setPEnabledLayerNames(layerVec)
                                .setPNext(&synchronization2Features);

    vkDevice = vkPhysicalDevice.createDeviceUnique(deviceDesc);
    // if (res != vk::Result::eSuccess) {
    //    return false;
    //}

    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkDevice.get());
    graphicsQueue = vkDevice->getQueue(graphicsQueueFamily, 0);
    // TODO Check for errors
    return true;
}

bool VKRenderer::findQueueFamilies(vk::PhysicalDevice physicalDevice)
{
    const auto props = physicalDevice.getQueueFamilyProperties();

    for (int i = 0; i < static_cast<int>(props.size()); i++) {
        const auto& queueFamily = props[i];

        if (graphicsQueueFamily == -1) {
            if (queueFamily.queueCount > 0 && (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)) {
                graphicsQueueFamily = i;
            }
        }
    }

    return graphicsQueueFamily != -1;
}

bool VKRenderer::createFrameResources()
{
    for (int i = 0; i < 3; ++i) {
        commandPool[i] = vkDevice->createCommandPoolUnique(vk::CommandPoolCreateInfo().setQueueFamilyIndex(graphicsQueueFamily));

        auto buffers = vkDevice->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo().setCommandPool(commandPool[i].get()).setCommandBufferCount(2));

        auto secondaryBuffers = vkDevice->allocateCommandBuffersUnique(
            vk::CommandBufferAllocateInfo().setCommandPool(commandPool[i].get()).setLevel(vk::CommandBufferLevel::eSecondary).setCommandBufferCount(2));

        cmdBuffers[i][cmdTransfer] = std::move(buffers[0]);
        cmdBuffers[i][cmdDraw] = std::move(buffers[1]);
        subpassCmdBuffers[i][subpassStencil] = std::move(secondaryBuffers[0]);
        subpassCmdBuffers[i][subpassColor] = std::move(secondaryBuffers[1]);

        fences[i][cmdTransfer] = vkDevice->createFenceUnique(vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled));
        fences[i][cmdDraw] = vkDevice->createFenceUnique(vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled));
    }

    transientCommandPool = vkDevice->createCommandPoolUnique(
        vk::CommandPoolCreateInfo().setFlags(vk::CommandPoolCreateFlagBits::eTransient).setQueueFamilyIndex(graphicsQueueFamily));

    m_instanceBuffer = VKInstanceBuffer(vkDevice.get(), graphicsQueue, transientCommandPool.get());

    return true;
}

void VKRenderer::createRenderPass()
{
    std::vector<vk::AttachmentDescription2> attachments = {
        vk::AttachmentDescription2()
            .setFormat(m_colorFormat)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal),
        vk::AttachmentDescription2()
            .setFormat(m_depthFormat)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setStencilLoadOp(m_settings.useOcclusionMesh() ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal),
    };

    if (m_settings.useVelocity()) {
        attachments.emplace_back(vk::AttachmentDescription2()
                                     .setFormat(m_velocityFormat)
                                     .setSamples(vk::SampleCountFlagBits::e1)
                                     .setLoadOp(vk::AttachmentLoadOp::eClear)
                                     .setStoreOp(vk::AttachmentStoreOp::eStore)
                                     .setInitialLayout(vk::ImageLayout::eUndefined)
                                     .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal));
    }

    const auto colorAttachment =
        vk::AttachmentReference2().setAttachment(0).setLayout(vk::ImageLayout::eColorAttachmentOptimal).setAspectMask(vk::ImageAspectFlagBits::eColor);
    const auto depthStencilAttachmentStencilWrite =
        vk::AttachmentReference2().setAttachment(1).setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal).setAspectMask(vk::ImageAspectFlagBits::eStencil);
    const auto depthStencilAttachment = vk::AttachmentReference2()
                                            .setAttachment(1)
                                            .setLayout(vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal)
                                            .setAspectMask(m_settings.useOcclusionMesh() ? vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil
                                                                                         : vk::ImageAspectFlagBits::eDepth);
    const auto velocityAttachment =
        vk::AttachmentReference2().setAttachment(2).setLayout(vk::ImageLayout::eColorAttachmentOptimal).setAspectMask(vk::ImageAspectFlagBits::eColor);

    std::vector<vk::AttachmentReference2> colorAttachments = {colorAttachment};

    if (m_settings.useVelocity()) {
        colorAttachments.emplace_back(velocityAttachment);
    }

    if (m_settings.useOcclusionMesh()) {
        const std::array<vk::SubpassDescription2, 2> subpasses = {
            vk::SubpassDescription2()
                .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
                .setViewMask(0)
                .setPDepthStencilAttachment(&depthStencilAttachmentStencilWrite),
            vk::SubpassDescription2()
                .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
                .setViewMask(0)
                .setColorAttachments(colorAttachments)
                .setPDepthStencilAttachment(&depthStencilAttachment),
        };

        const std::array<vk::SubpassDependency2, 1> dependencies = {
            vk::SubpassDependency2()
                .setSrcSubpass(0)
                .setDstSubpass(1)
                .setSrcStageMask(vk::PipelineStageFlagBits::eLateFragmentTests)
                .setDstStageMask(vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests)
                .setSrcAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite)
                .setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite)
                .setViewOffset(0),
        };

        vk::RenderPassCreateInfo2 createInfo;
        createInfo.setAttachments(attachments).setSubpasses(subpasses).setDependencies(dependencies);
        renderPass = vkDevice->createRenderPass2Unique(createInfo);
    } else {
        const auto subpass = vk::SubpassDescription2()
                                 .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
                                 .setViewMask(0)
                                 .setColorAttachments(colorAttachments)
                                 .setPDepthStencilAttachment(&depthStencilAttachment);

        vk::RenderPassCreateInfo2 createInfo;
        createInfo.setAttachments(attachments).setSubpasses(subpass);
        renderPass = vkDevice->createRenderPass2Unique(createInfo);
    }
}

bool VKRenderer::createGraphicsPipelines()
{
    CHECK(createGridPipeline());
    CHECK(createScenePipeline());
    CHECK(createStencilPipeline());
    return true;
}

bool VKRenderer::createGridPipeline()
{
    gridFragmentShader = vkDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo().setCode(gridFrag));
    gridVertexShader = vkDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo().setCode(gridVert));

    {
        const auto pushConstantRange =
            vk::PushConstantRange().setStageFlags(vk::ShaderStageFlagBits::eVertex).setOffset(0).setSize(offsetof(ShaderPushConstants, viewportSize));

        const auto layoutCreateInfo = vk::PipelineLayoutCreateInfo().setPushConstantRanges(pushConstantRange);
        gridPipelineLayout = vkDevice->createPipelineLayoutUnique(layoutCreateInfo);
    }

    const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
        vk::PipelineShaderStageCreateInfo().setStage(vk::ShaderStageFlagBits::eVertex).setModule(gridVertexShader.get()).setPName("main"),
        vk::PipelineShaderStageCreateInfo().setStage(vk::ShaderStageFlagBits::eFragment).setModule(gridFragmentShader.get()).setPName("main"),
    };

    const std::array<vk::VertexInputBindingDescription, 1> inputBindings = {
        vk::VertexInputBindingDescription().setBinding(0).setStride(sizeof(Geometry::Vertex)).setInputRate(vk::VertexInputRate::eVertex),
    };

    const std::array<vk::VertexInputAttributeDescription, 2> inputAttributes = {
        vk::VertexInputAttributeDescription()
            .setLocation(0)
            .setBinding(0)
            .setFormat(vk::Format::eR32G32B32Sfloat)
            .setOffset(offsetof(Geometry::Vertex, position)),
        vk::VertexInputAttributeDescription()
            .setLocation(1)
            .setBinding(0)
            .setFormat(vk::Format::eR32G32B32Sfloat)
            .setOffset(offsetof(Geometry::Vertex, normal)),
    };

    const auto vertexInputState =
        vk::PipelineVertexInputStateCreateInfo().setVertexBindingDescriptions(inputBindings).setVertexAttributeDescriptions(inputAttributes);

    const auto inputAssemblyState =
        vk::PipelineInputAssemblyStateCreateInfo().setTopology(vk::PrimitiveTopology::eTriangleList).setPrimitiveRestartEnable(VK_FALSE);

    const auto viewportState = vk::PipelineViewportStateCreateInfo().setViewportCount(1).setScissorCount(1);

    const auto rasterizationState = vk::PipelineRasterizationStateCreateInfo()
                                        .setDepthClampEnable(VK_FALSE)
                                        .setRasterizerDiscardEnable(VK_FALSE)
                                        .setPolygonMode(vk::PolygonMode::eFill)
                                        .setCullMode(vk::CullModeFlagBits::eBack)
                                        .setFrontFace(vk::FrontFace::eCounterClockwise)
                                        .setDepthBiasEnable(VK_FALSE)
                                        .setLineWidth(1.f);

    const auto multisampleState =
        vk::PipelineMultisampleStateCreateInfo().setRasterizationSamples(vk::SampleCountFlagBits::e1).setSampleShadingEnable(VK_FALSE);

    const auto depthStencilState = vk::PipelineDepthStencilStateCreateInfo()
                                       .setDepthTestEnable(VK_FALSE)
                                       .setStencilTestEnable(m_settings.useOcclusionMesh() ? VK_TRUE : VK_FALSE)
                                       .setFront(vk::StencilOpState()
                                                     .setFailOp(vk::StencilOp::eKeep)
                                                     .setPassOp(vk::StencilOp::eKeep)
                                                     .setDepthFailOp(vk::StencilOp::eKeep)
                                                     .setCompareOp(vk::CompareOp::eNotEqual)
                                                     .setCompareMask(0xff)
                                                     .setWriteMask(0)
                                                     .setReference(1));

    const std::vector<vk::PipelineColorBlendAttachmentState> colorAttachments(m_settings.useVelocity() ? 2 : 1,
        vk::PipelineColorBlendAttachmentState().setBlendEnable(VK_FALSE).setColorWriteMask(
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA));

    const auto colorBlendState = vk::PipelineColorBlendStateCreateInfo().setLogicOpEnable(VK_FALSE).setAttachments(colorAttachments);

    const std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const auto dynamicState = vk::PipelineDynamicStateCreateInfo().setDynamicStates(dynamicStates);

    vk::GraphicsPipelineCreateInfo createInfo;
    createInfo.setStages(stages)
        .setPVertexInputState(&vertexInputState)
        .setPInputAssemblyState(&inputAssemblyState)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizationState)
        .setPMultisampleState(&multisampleState)
        .setPDepthStencilState(&depthStencilState)
        .setPColorBlendState(&colorBlendState)
        .setPDynamicState(&dynamicState)
        .setLayout(gridPipelineLayout.get())
        .setRenderPass(renderPass.get())
        .setSubpass(m_settings.useOcclusionMesh() ? 1 : 0);

    auto result = vkDevice->createGraphicsPipelineUnique({}, createInfo);
    if (result.result != vk::Result::eSuccess) {
        return false;
    }

    gridPipeline = std::move(result.value);
    return true;
}

bool VKRenderer::createScenePipeline()
{
    if (m_settings.useVelocity()) {
        sceneFragmentShader = vkDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo().setCode(sceneVelocityFrag));
        sceneVertexShader = vkDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo().setCode(sceneVelocityVert));
    } else {
        sceneFragmentShader = vkDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo().setCode(sceneNoVelocityFrag));
        sceneVertexShader = vkDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo().setCode(sceneNoVelocityVert));
    }

    {
        const auto pushConstantRange =
            vk::PushConstantRange().setStageFlags(vk::ShaderStageFlagBits::eVertex).setOffset(0).setSize(sizeof(ShaderPushConstants));

        const auto layoutCreateInfo = vk::PipelineLayoutCreateInfo().setPushConstantRanges(pushConstantRange);
        scenePipelineLayout = vkDevice->createPipelineLayoutUnique(layoutCreateInfo);
    }

    const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
        vk::PipelineShaderStageCreateInfo().setStage(vk::ShaderStageFlagBits::eVertex).setModule(sceneVertexShader.get()).setPName("main"),
        vk::PipelineShaderStageCreateInfo().setStage(vk::ShaderStageFlagBits::eFragment).setModule(sceneFragmentShader.get()).setPName("main"),
    };

    const std::array<vk::VertexInputBindingDescription, 2> inputBindings = {
        vk::VertexInputBindingDescription().setBinding(0).setStride(sizeof(Geometry::Vertex)).setInputRate(vk::VertexInputRate::eVertex),
        vk::VertexInputBindingDescription().setBinding(1).setStride(sizeof(ObjectRenderData)).setInputRate(vk::VertexInputRate::eInstance),
    };

    const std::array<vk::VertexInputAttributeDescription, 10> inputAttributes = {
        vk::VertexInputAttributeDescription()
            .setLocation(0)
            .setBinding(0)
            .setFormat(vk::Format::eR32G32B32Sfloat)
            .setOffset(offsetof(Geometry::Vertex, position)),
        vk::VertexInputAttributeDescription()
            .setLocation(1)
            .setBinding(0)
            .setFormat(vk::Format::eR32G32B32Sfloat)
            .setOffset(offsetof(Geometry::Vertex, normal)),
        vk::VertexInputAttributeDescription()
            .setLocation(2)
            .setBinding(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(ObjectRenderData, world)),
        vk::VertexInputAttributeDescription()
            .setLocation(3)
            .setBinding(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(ObjectRenderData, world) + 4 * sizeof(float)),
        vk::VertexInputAttributeDescription()
            .setLocation(4)
            .setBinding(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(ObjectRenderData, world) + 8 * sizeof(float)),
        vk::VertexInputAttributeDescription()
            .setLocation(5)
            .setBinding(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(ObjectRenderData, world) + 12 * sizeof(float)),
        vk::VertexInputAttributeDescription()
            .setLocation(6)
            .setBinding(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(ObjectRenderData, nextFrameWorld)),
        vk::VertexInputAttributeDescription()
            .setLocation(7)
            .setBinding(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(ObjectRenderData, nextFrameWorld) + 4 * sizeof(float)),
        vk::VertexInputAttributeDescription()
            .setLocation(8)
            .setBinding(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(ObjectRenderData, nextFrameWorld) + 8 * sizeof(float)),
        vk::VertexInputAttributeDescription()
            .setLocation(9)
            .setBinding(1)
            .setFormat(vk::Format::eR32G32B32A32Sfloat)
            .setOffset(offsetof(ObjectRenderData, nextFrameWorld) + 12 * sizeof(float)),
    };

    const auto vertexInputState =
        vk::PipelineVertexInputStateCreateInfo().setVertexBindingDescriptions(inputBindings).setVertexAttributeDescriptions(inputAttributes);

    const auto inputAssemblyState =
        vk::PipelineInputAssemblyStateCreateInfo().setTopology(vk::PrimitiveTopology::eTriangleList).setPrimitiveRestartEnable(VK_FALSE);

    const auto viewportState = vk::PipelineViewportStateCreateInfo().setViewportCount(1).setScissorCount(1);

    const auto rasterizationState = vk::PipelineRasterizationStateCreateInfo()
                                        .setDepthClampEnable(VK_FALSE)
                                        .setRasterizerDiscardEnable(VK_FALSE)
                                        .setPolygonMode(vk::PolygonMode::eFill)
                                        .setCullMode(vk::CullModeFlagBits::eBack)
                                        .setFrontFace(vk::FrontFace::eCounterClockwise)
                                        .setDepthBiasEnable(VK_FALSE)
                                        .setLineWidth(1.f);

    const auto multisampleState =
        vk::PipelineMultisampleStateCreateInfo().setRasterizationSamples(vk::SampleCountFlagBits::e1).setSampleShadingEnable(VK_FALSE);

    const auto depthStencilState = vk::PipelineDepthStencilStateCreateInfo()
                                       .setDepthTestEnable(VK_TRUE)
                                       .setDepthWriteEnable(VK_TRUE)
                                       .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
                                       .setDepthBoundsTestEnable(VK_FALSE)
                                       .setStencilTestEnable(m_settings.useOcclusionMesh() ? VK_TRUE : VK_FALSE)
                                       .setFront(vk::StencilOpState()
                                                     .setFailOp(vk::StencilOp::eKeep)
                                                     .setPassOp(vk::StencilOp::eKeep)
                                                     .setDepthFailOp(vk::StencilOp::eKeep)
                                                     .setCompareOp(vk::CompareOp::eNotEqual)
                                                     .setCompareMask(0xff)
                                                     .setWriteMask(0)
                                                     .setReference(1));

    const std::vector<vk::PipelineColorBlendAttachmentState> colorAttachments(m_settings.useVelocity() ? 2 : 1,
        vk::PipelineColorBlendAttachmentState().setBlendEnable(VK_FALSE).setColorWriteMask(
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA));

    const auto colorBlendState = vk::PipelineColorBlendStateCreateInfo().setLogicOpEnable(VK_FALSE).setAttachments(colorAttachments);

    const std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const auto dynamicState = vk::PipelineDynamicStateCreateInfo().setDynamicStates(dynamicStates);

    vk::GraphicsPipelineCreateInfo createInfo;
    createInfo.setStages(stages)
        .setPVertexInputState(&vertexInputState)
        .setPInputAssemblyState(&inputAssemblyState)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizationState)
        .setPMultisampleState(&multisampleState)
        .setPDepthStencilState(&depthStencilState)
        .setPColorBlendState(&colorBlendState)
        .setPDynamicState(&dynamicState)
        .setLayout(scenePipelineLayout.get())
        .setRenderPass(renderPass.get())
        .setSubpass(m_settings.useOcclusionMesh() ? 1 : 0);

    auto result = vkDevice->createGraphicsPipelineUnique({}, createInfo);
    if (result.result != vk::Result::eSuccess) {
        return false;
    }

    scenePipeline = std::move(result.value);
    return true;
}

bool VKRenderer::createStencilPipeline()
{
    if (!m_settings.useOcclusionMesh()) {
        return true;
    }

    stencilFragmentShader = vkDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo().setCode(stencilFrag));
    stencilVertexShader = vkDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo().setCode(stencilVert));

    stencilPipelineLayout = vkDevice->createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo());
    const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
        vk::PipelineShaderStageCreateInfo().setStage(vk::ShaderStageFlagBits::eVertex).setModule(stencilVertexShader.get()).setPName("main"),
        vk::PipelineShaderStageCreateInfo().setStage(vk::ShaderStageFlagBits::eFragment).setModule(stencilFragmentShader.get()).setPName("main"),
    };

    const std::array<vk::VertexInputBindingDescription, 1> inputBindings = {
        vk::VertexInputBindingDescription().setBinding(0).setStride(sizeof(varjo_Vector2Df)).setInputRate(vk::VertexInputRate::eVertex),
    };

    const std::array<vk::VertexInputAttributeDescription, 1> inputAttributes = {
        vk::VertexInputAttributeDescription().setLocation(0).setBinding(0).setFormat(vk::Format::eR32G32Sfloat).setOffset(0),
    };

    const auto vertexInputState =
        vk::PipelineVertexInputStateCreateInfo().setVertexBindingDescriptions(inputBindings).setVertexAttributeDescriptions(inputAttributes);

    const auto inputAssemblyState =
        vk::PipelineInputAssemblyStateCreateInfo().setTopology(vk::PrimitiveTopology::eTriangleList).setPrimitiveRestartEnable(VK_FALSE);

    const auto viewportState = vk::PipelineViewportStateCreateInfo().setViewportCount(1).setScissorCount(1);

    const auto rasterizationState = vk::PipelineRasterizationStateCreateInfo()
                                        .setDepthClampEnable(VK_FALSE)
                                        .setRasterizerDiscardEnable(VK_FALSE)
                                        .setPolygonMode(vk::PolygonMode::eFill)
                                        .setCullMode(vk::CullModeFlagBits::eBack)
                                        .setFrontFace(vk::FrontFace::eCounterClockwise)
                                        .setDepthBiasEnable(VK_FALSE)
                                        .setLineWidth(1.f);

    const auto multisampleState =
        vk::PipelineMultisampleStateCreateInfo().setRasterizationSamples(vk::SampleCountFlagBits::e1).setSampleShadingEnable(VK_FALSE);

    const auto depthStencilState =
        vk::PipelineDepthStencilStateCreateInfo().setDepthTestEnable(VK_FALSE).setStencilTestEnable(VK_TRUE).setFront(vk::StencilOpState()
                                                                                                                          .setFailOp(vk::StencilOp::eKeep)
                                                                                                                          .setPassOp(vk::StencilOp::eReplace)
                                                                                                                          .setDepthFailOp(vk::StencilOp::eKeep)
                                                                                                                          .setCompareOp(vk::CompareOp::eAlways)
                                                                                                                          .setCompareMask(0xff)
                                                                                                                          .setWriteMask(0xff)
                                                                                                                          .setReference(1));

    const std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const auto dynamicState = vk::PipelineDynamicStateCreateInfo().setDynamicStates(dynamicStates);

    vk::GraphicsPipelineCreateInfo createInfo;
    createInfo.setStages(stages)
        .setPVertexInputState(&vertexInputState)
        .setPInputAssemblyState(&inputAssemblyState)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizationState)
        .setPMultisampleState(&multisampleState)
        .setPDepthStencilState(&depthStencilState)
        .setPDynamicState(&dynamicState)
        .setLayout(stencilPipelineLayout.get())
        .setRenderPass(renderPass.get())
        .setSubpass(0);

    auto result = vkDevice->createGraphicsPipelineUnique({}, createInfo);
    if (result.result != vk::Result::eSuccess) {
        return false;
    }

    stencilPipeline = std::move(result.value);
    return true;
}

void VKRenderer::createOcclusionMeshGeometry(uint32_t viewIndex)
{
    if (!m_settings.useOcclusionMesh()) {
        return;
    }

    varjo_Mesh2Df* mesh = varjo_CreateOcclusionMesh(m_session, viewIndex, varjo_WindingOrder_CounterClockwise);
    m_occlusionMeshGeometry[viewIndex] = VKOcclusionMeshGeometry(vkPhysicalDevice, vkDevice.get(), graphicsQueue, transientCommandPool.get(), mesh);
    varjo_FreeOcclusionMesh(mesh);
}

void VKRenderer::recreateOcclusionMesh(uint32_t viewIndex) { createOcclusionMeshGeometry(viewIndex); }

/**
 * Vulkan Renderer
 */

VKRenderer::VKRenderer(varjo_Session* session, const RendererSettings& rendererSettings)
    : IRenderer(session, rendererSettings)
{
}

VKRenderer::~VKRenderer() { freeRendererResources(); }

std::shared_ptr<Geometry> VKRenderer::createGeometry(uint32_t vertexCount, uint32_t indexCount)
{
    return std::make_shared<VKGeometry>(vkPhysicalDevice, vkDevice.get(), graphicsQueue, transientCommandPool.get(), vertexCount, indexCount);
}

std::shared_ptr<RenderTexture> VKRenderer::createColorTexture(int32_t width, int32_t height, varjo_Texture colorTexture)
{
    VkImage image = varjo_ToVkTexture(colorTexture);
    return std::make_shared<VKColorRenderTexture>(vkDevice.get(), width, height, image, m_colorFormat);
}

std::shared_ptr<RenderTexture> VKRenderer::createDepthTexture(int32_t width, int32_t height, varjo_Texture depthTexture)
{
    VkImage image = varjo_ToVkTexture(depthTexture);
    return std::make_shared<VKDepthRenderTexture>(vkPhysicalDevice, vkDevice.get(), width, height, image, m_depthFormat, m_hasStencil);
}

std::shared_ptr<RenderTexture> VKRenderer::createVelocityTexture(int32_t width, int32_t height, varjo_Texture velocityTexture)
{
    VkImage image = varjo_ToVkTexture(velocityTexture);
    return std::make_shared<VKColorRenderTexture>(vkDevice.get(), width, height, image, m_velocityFormat);
}

bool VKRenderer::isVrsSupported() const { return false; }

void VKRenderer::finishRendering() { vkDevice->waitIdle(); }

VkDevice VKRenderer::getDevice() const { return vkDevice.get(); }

bool VKRenderer::initVarjo()
{
    if (!initialize(m_session)) {
        return false;
    }
    createSwapchains();

    return true;
}

void VKRenderer::createSwapchains()
{
    // create color texture swap chain
    m_swapChainConfig.numberOfTextures = 3;
    m_swapChainConfig.textureArraySize = 1;
    m_swapChainConfig.textureFormat = m_settings.noSrgb() ? varjo_TextureFormat_R8G8B8A8_UNORM : varjo_TextureFormat_R8G8B8A8_SRGB;
    m_swapChainConfig.textureWidth = getTotalViewportsWidth();
    m_swapChainConfig.textureHeight = getTotalViewportsHeight();

    m_colorSwapChain = varjo_VKCreateSwapChain(m_session, vkDevice.get(), graphicsQueueFamily, 0, &m_swapChainConfig);

    if (m_settings.useDepthLayers()) {
        m_depthSwapChainConfig = m_swapChainConfig;
        m_depthSwapChainConfig.textureFormat = m_settings.depthFormat();
        m_depthSwapChain = varjo_VKCreateSwapChain(m_session, vkDevice.get(), graphicsQueueFamily, 0, &m_depthSwapChainConfig);
    }

    if (m_settings.useVelocity()) {
        m_velocitySwapChainConfig = m_swapChainConfig;
        m_velocitySwapChainConfig.textureFormat = varjo_VelocityTextureFormat_R8G8B8A8_UINT;
        m_velocitySwapChain = varjo_VKCreateSwapChain(m_session, vkDevice.get(), graphicsQueueFamily, 0, &m_velocitySwapChainConfig);
    }
}

varjo_SwapChain* VKRenderer::createSwapChain(varjo_SwapChainConfig2& swapchainConfig)
{
    return varjo_VKCreateSwapChain(m_session, vkDevice.get(), graphicsQueueFamily, 0, &swapchainConfig);
}

void VKRenderer::bindRenderTarget(const RenderTargetTextures& renderTarget)
{
    frameNumber++;
    const std::array<vk::Fence, numCommands> fencesToWait = {fences[currentFrameIndex()][cmdTransfer].get(), fences[currentFrameIndex()][cmdDraw].get()};
    if (vkDevice->waitForFences(fencesToWait, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess) {
        std::printf("Error waiting for fences\n");
        abort();
    }

    vkDevice->resetFences(fencesToWait);
    vkDevice->resetCommandPool(commandPool[currentFrameIndex()].get());

    m_currentRenderTarget = renderTarget;

    const std::set<RenderTexture*> framebufferKey = {renderTarget.getColorTexture().get(), renderTarget.getDepthTexture().get()};
    const auto framebufferIt = m_framebuffers.find(framebufferKey);
    if (framebufferIt == m_framebuffers.cend()) {
        auto colorTarget = std::static_pointer_cast<VKColorRenderTexture>(renderTarget.getColorTexture());
        auto depthTarget = std::static_pointer_cast<VKDepthRenderTexture>(renderTarget.getDepthTexture());

        std::vector<vk::ImageView> attachments = {
            colorTarget->getImageView(),
            depthTarget->getImageView(),
        };

        if (m_settings.useVelocity()) {
            auto velocityTarget = std::static_pointer_cast<VKColorRenderTexture>(renderTarget.getVelocityTexture());
            attachments.emplace_back(velocityTarget->getImageView());
        }

        const auto createInfo = vk::FramebufferCreateInfo()
                                    .setRenderPass(renderPass.get())
                                    .setAttachments(attachments)
                                    .setWidth(colorTarget->width())
                                    .setHeight(colorTarget->height())
                                    .setLayers(1);

        vk::UniqueFramebuffer framebuffer = vkDevice->createFramebufferUnique(createInfo);
        m_currentFramebuffer = framebuffer.get();
        m_framebuffers[framebufferKey] = std::move(framebuffer);
    } else {
        m_currentFramebuffer = framebufferIt->second.get();
    }
}

void VKRenderer::unbindRenderTarget() {}

void VKRenderer::freeCurrentRenderTarget() { m_currentRenderTarget.reset(); }

void VKRenderer::clearRenderTarget(const RenderTargetTextures& renderTarget, float r, float g, float b, float a) { m_clearColor = {r, g, b, a}; }

void VKRenderer::useGeometry(const std::shared_ptr<Geometry>& geometry) { m_currentGeometry = std::static_pointer_cast<VKGeometry, Geometry>(geometry); }

void VKRenderer::setupCamera(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
{
    m_pushConstants.viewMatrix = viewMatrix;
    m_pushConstants.projectionMatrix = projectionMatrix;
}

void VKRenderer::setViewport(const varjo_Viewport& viewport)
{
    m_currentViewport = vk::Rect2D{{viewport.x, viewport.y}, {static_cast<uint32_t>(viewport.width), static_cast<uint32_t>(viewport.height)}};
    m_pushConstants.viewportSize = {viewport.width, viewport.height};
}

void VKRenderer::updateVrsMap(const varjo_Viewport& viewport)
{
    // TODO;
    assert(false);
}

void VKRenderer::uploadInstanceBuffer(const std::vector<std::vector<ObjectRenderData>>& matrices)
{
    m_instancedDrawsOffsetCount.resize(0);
    m_instancedDrawsOffsetCount.reserve(matrices.size());
    std::size_t dataSize = 0;
    for (const auto& singleDrawMatrices : matrices) {
        dataSize += singleDrawMatrices.size();
    }
    std::vector<ObjectRenderData> instanceBufferData;
    instanceBufferData.reserve(dataSize);
    std::size_t dataOffset = 0;
    for (const auto& singleDrawMatrices : matrices) {
        m_instancedDrawsOffsetCount.push_back(std::make_pair(dataOffset, singleDrawMatrices.size()));
        dataOffset += singleDrawMatrices.size() * sizeof(ObjectRenderData);
        instanceBufferData.insert(instanceBufferData.end(), singleDrawMatrices.begin(), singleDrawMatrices.end());
    }

    m_instanceBuffer.transferInstanceData(
        vkPhysicalDevice, instanceBufferData, cmdBuffers[currentFrameIndex()][cmdTransfer].get(), fences[currentFrameIndex()][cmdTransfer].get());
}

void VKRenderer::setViewportCommands(vk::CommandBuffer cmdBuffer)
{
    const vk::Viewport viewport{static_cast<float>(m_currentViewport.offset.x),
        static_cast<float>(m_currentViewport.offset.y + m_currentViewport.extent.height), static_cast<float>(m_currentViewport.extent.width),
        -static_cast<float>(m_currentViewport.extent.height), 0.f, 1.f};
    cmdBuffer.setViewport(0, viewport);
    cmdBuffer.setScissor(0, m_currentViewport);
}

void VKRenderer::renderOcclusionMesh()
{
    if (m_settings.useOcclusionMesh() && m_currentViewIndex < 2 && m_occlusionMeshGeometry[m_currentViewIndex].getVertexCount() > 0) {
        auto cmdBuffer = subpassCmdBuffers[currentFrameIndex()][subpassStencil].get();
        cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, stencilPipeline.get());
        setViewportCommands(cmdBuffer);
        m_occlusionMeshGeometry[m_currentViewIndex].bind(cmdBuffer, 0);
        cmdBuffer.draw(m_occlusionMeshGeometry[m_currentViewIndex].getVertexCount(), 1, 0, 0);
    }
}

void VKRenderer::drawGrid()
{
    auto cmdBuffer = subpassCmdBuffers[currentFrameIndex()][subpassColor].get();
    cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, gridPipeline.get());
    setViewportCommands(cmdBuffer);
    cmdBuffer.pushConstants(gridPipelineLayout.get(), vk::ShaderStageFlagBits::eVertex, 0, offsetof(ShaderPushConstants, viewportSize), &m_pushConstants);
    m_currentGeometry->bind(cmdBuffer, 0);
    cmdBuffer.drawIndexed(m_currentGeometry->indexCount(), 1, 0, 0, 0);

    cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, scenePipeline.get());
    setViewportCommands(cmdBuffer);
    cmdBuffer.pushConstants(scenePipelineLayout.get(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(ShaderPushConstants), &m_pushConstants);
}

void VKRenderer::drawObjects(std::size_t objectsIndex)
{
    auto cmdBuffer = subpassCmdBuffers[currentFrameIndex()][subpassColor].get();
    m_currentGeometry->bind(cmdBuffer, 0);

    const auto drawOffsetCount = m_instancedDrawsOffsetCount[objectsIndex];
    m_instanceBuffer.bind(cmdBuffer, 1, drawOffsetCount.first);

    cmdBuffer.drawIndexed(m_currentGeometry->indexCount(), static_cast<uint32_t>(drawOffsetCount.second), 0, 0, 0);
}

void VKRenderer::drawMirrorWindow()
{
    // TODO
    assert(false);
}

void VKRenderer::advance() {}

void VKRenderer::preRenderFrame()
{
    if (m_settings.useOcclusionMesh()) {
        auto cmdBuffer = subpassCmdBuffers[currentFrameIndex()][subpassStencil].get();
        const auto inheritanceInfo = vk::CommandBufferInheritanceInfo()
                                         .setRenderPass(renderPass.get())
                                         .setSubpass(0)
                                         .setFramebuffer(m_currentFramebuffer)
                                         .setOcclusionQueryEnable(VK_FALSE);

        cmdBuffer.begin(vk::CommandBufferBeginInfo()
                            .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit | vk::CommandBufferUsageFlagBits::eRenderPassContinue)
                            .setPInheritanceInfo(&inheritanceInfo));
    }

    auto cmdBuffer = subpassCmdBuffers[currentFrameIndex()][subpassColor].get();
    const auto inheritanceInfo = vk::CommandBufferInheritanceInfo()
                                     .setRenderPass(renderPass.get())
                                     .setSubpass(m_settings.useOcclusionMesh() ? 1 : 0)
                                     .setFramebuffer(m_currentFramebuffer)
                                     .setOcclusionQueryEnable(VK_FALSE);

    cmdBuffer.begin(vk::CommandBufferBeginInfo()
                        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit | vk::CommandBufferUsageFlagBits::eRenderPassContinue)
                        .setPInheritanceInfo(&inheritanceInfo));
}

void VKRenderer::postRenderFrame()
{
    if (m_settings.useOcclusionMesh()) {
        subpassCmdBuffers[currentFrameIndex()][subpassStencil]->end();
    }

    subpassCmdBuffers[currentFrameIndex()][subpassColor]->end();

    const auto renderTexture = m_currentRenderTarget.getColorTexture();

    auto cmdBuffer = cmdBuffers[currentFrameIndex()][cmdDraw].get();
    cmdBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    std::vector<vk::ClearValue> clearValues = {
        vk::ClearColorValue(m_clearColor),
        vk::ClearDepthStencilValue().setDepth(1.f).setStencil(0),
    };

    if (m_settings.useVelocity()) {
        clearValues.emplace_back(clearValues[0]);
    }

    cmdBuffer.beginRenderPass2(vk::RenderPassBeginInfo()
                                   .setRenderPass(renderPass.get())
                                   .setFramebuffer(m_currentFramebuffer)
                                   .setRenderArea(vk::Rect2D().setOffset({0, 0}).setExtent(
                                       {static_cast<uint32_t>(renderTexture->width()), static_cast<uint32_t>(renderTexture->height())}))
                                   .setClearValues(clearValues),
        vk::SubpassBeginInfo().setContents(vk::SubpassContents::eSecondaryCommandBuffers));

    if (m_settings.useOcclusionMesh()) {
        cmdBuffer.executeCommands(subpassCmdBuffers[currentFrameIndex()][subpassStencil].get());
        cmdBuffer.nextSubpass2(vk::SubpassBeginInfo().setContents(vk::SubpassContents::eSecondaryCommandBuffers), vk::SubpassEndInfo());
    }

    cmdBuffer.executeCommands(subpassCmdBuffers[currentFrameIndex()][subpassColor].get());

    cmdBuffer.endRenderPass2(vk::SubpassEndInfo());
    cmdBuffer.end();

    const auto submitInfo = vk::CommandBufferSubmitInfoKHR().setCommandBuffer(cmdBuffer);
    graphicsQueue.submit2KHR(vk::SubmitInfo2KHR().setCommandBufferInfos(submitInfo), fences[currentFrameIndex()][cmdDraw].get());
}

varjo_ClipRange VKRenderer::getClipRange() const { return varjo_ClipRangeZeroToOne; }

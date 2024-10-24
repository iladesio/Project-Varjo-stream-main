#pragma once

#include <d3d12.h>
#include <memory>
#include <wrl/client.h>
#include <dxgi1_6.h>
#include <array>

#include <Varjo_d3d11.h>
#include "IRenderer.hpp"

#include "Config.hpp"
#include "Window.hpp"

#ifdef USE_EXPERIMENTAL_API
#include <Varjo_experimental.h>
#endif

class Texture2D;
class D3D12DynamicBuffer;
class DescriptorHeap;

// Varjo runtime allows only 1 frame to be in flight, so 2 is enough (one is rendering, one is in queue)
constexpr int c_D3D12RingBufferSize = 2;

// Number of GPU nodes we use in SLI
constexpr int c_D3D12RenderingNodesInSli = 2;

struct Descriptor {
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};

    bool isNull() const { return cpuHandle.ptr == 0; }

    operator bool() const { return !isNull(); }

    operator D3D12_CPU_DESCRIPTOR_HANDLE() const { return cpuHandle; }
    operator D3D12_GPU_DESCRIPTOR_HANDLE() const { return gpuHandle; }

    UINT slotIndex{0};
    bool allocated{false};
};

class IDescriptorAllocator
{
public:
    virtual ~IDescriptorAllocator() = default;
    virtual Descriptor allocateFromHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) const = 0;
    virtual DescriptorHeap* getHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) const = 0;
};

// Holds all of the node-specific rendering resources
class GpuNode : public IDescriptorAllocator
{
public:
    GpuNode(uint32_t nodeIndex, const Microsoft::WRL::ComPtr<ID3D12Device2>& device, varjo_Session* session);
    ~GpuNode();

    struct PerFrameResources {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
        Microsoft::WRL::ComPtr<ID3D12Resource> instanceBuffer;
        std::vector<std::pair<std::size_t, std::size_t>> instancedObjectsOffsetCount;
        uint64_t fenceValue{};
        int backBufferIndex{0};
    };

    void waitForGpu();
    Descriptor allocateFromHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) const override;
    DescriptorHeap* getHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) const override;
    void completeFrameRender();

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> getCommandList() const { return m_commandList; }
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> getCommandQueue() const { return m_queue; }
    Microsoft::WRL::ComPtr<ID3D12Fence> fence() const { return m_frameFence; }

    uint32_t nodeMask() const { return m_nodeMask; }
    uint32_t index() const { return m_nodeIndex; }
    HANDLE fenceEvent() const { return m_fenceEvent; }
    int64_t lastSignalledFenceValue() const { return m_frameNumber; }
    PerFrameResources& currentFrameResources() { return m_perFrameResources[m_frameRingIndex]; }
    UINT getOcclusionMeshVertexCount(int32_t viewIndex) const { return m_occlusionMeshVertexCount[viewIndex]; }
    D3D12_GPU_VIRTUAL_ADDRESS getOcclusionMeshGPUVirtualAddress(int32_t viewIndex) const { return m_occlusionMeshBuffers[viewIndex]->GetGPUVirtualAddress(); }
    void createOcclusionMeshResources(varjo_Session* session, uint32_t viewIndex);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> createUploadBuffer(size_t size) const;
    void updateFrameRingIndex();
    void createResources(varjo_Session* session);

    Microsoft::WRL::ComPtr<ID3D12Device2> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_frameFence;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_computeQueue;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_computeFence;

    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> m_occlusionMeshBuffers{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> m_occlusionMeshUploadBuffers{};
    std::array<int32_t, 2> m_occlusionMeshVertexCount{};

    std::unique_ptr<DescriptorHeap> m_rtvs;
    std::unique_ptr<DescriptorHeap> m_dsvs;
    std::unique_ptr<DescriptorHeap> m_uavs;
    HANDLE m_fenceEvent{};

    std::array<PerFrameResources, c_D3D12RingBufferSize> m_perFrameResources;
    unsigned int m_descriptorFreeSlotIndex{0};
    uint32_t m_nodeIndex{0};
    uint32_t m_nodeMask{0x1};
    int m_frameRingIndex{0};
    int64_t m_frameNumber{0};
};

// Handles cross gpu node copy from srcGpuNode to dstGpuNode
class CrossNodeCopier
{
public:
    CrossNodeCopier(const Microsoft::WRL::ComPtr<ID3D12Device2>& device, GpuNode* srcGpuNode, GpuNode* dstGpuNode, bool useDepthLayers, bool useVelocity);
    void recordViewportBoxForCopy(D3D12_BOX box) { m_viewportBoxes.push_back(box); }
    void copy(const RenderTargetTextures& renderTarget);

private:
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, c_D3D12RingBufferSize> m_commandAllocators;
    uint32_t m_allocatorIndex{0};
    GpuNode* m_srcGpuNode{nullptr};  // textures are read from this node
    GpuNode* m_dstGpuNode{nullptr};  // copy executing node
    std::vector<D3D12_BOX> m_viewportBoxes;
    bool m_useDepthLayers{false};
    bool m_useVelocity{false};
};

class D3D12RenderTextureSingleNode
{
public:
    D3D12RenderTextureSingleNode(std::shared_ptr<Texture2D> texture);
    void linkSharedResource(D3D12RenderTextureSingleNode* otherNodeRenderTarget);

    std::shared_ptr<Texture2D> dxTexture() const { return m_Texture; }
    Microsoft::WRL::ComPtr<ID3D12Resource> dxCrossNodeTexture() const { return m_crossNodeTexture; }

private:
    std::shared_ptr<Texture2D> m_Texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_crossNodeTexture;  // handle for texture available on another gpu
};

class D3D12RenderTexture : public RenderTexture
{
public:
    D3D12RenderTexture(int width, int height, std::array<std::unique_ptr<D3D12RenderTextureSingleNode>, c_D3D12RenderingNodesInSli>&& renderTextures);
    varjo_Texture texture() const override;
    std::shared_ptr<Texture2D> dxTexture(uint32_t nodeIndex) const;
    Microsoft::WRL::ComPtr<ID3D12Resource> dxCrossNodeTexture(uint32_t nodeIndex) const;

private:
    std::array<std::unique_ptr<D3D12RenderTextureSingleNode>, c_D3D12RenderingNodesInSli> m_renderTextures;
};

class D3D12Renderer final : public IRenderer
{
public:
    D3D12Renderer(varjo_Session* session, const RendererSettings& rendererSettings);
    ~D3D12Renderer() override;

    std::shared_ptr<RenderTexture> createColorTexture(int32_t width, int32_t height, varjo_Texture colorTexture) override;
    std::shared_ptr<RenderTexture> createDepthTexture(int32_t width, int32_t height, varjo_Texture depthTexture) override;
    std::shared_ptr<RenderTexture> createVelocityTexture(int32_t width, int32_t height, varjo_Texture velocityTexture) override;

    Microsoft::WRL::ComPtr<ID3D12Device2> getDevice() const { return m_device; }
    uint32_t getNodeCount() const { return m_nodeCount; }
    GpuNode* getGpuNode(uint32_t nodeIndex) { return m_gpuNodes[nodeIndex].get(); }
    uint32_t getSharedGpuMask() const { return m_sharedGpumask; }

    std::shared_ptr<Geometry> createGeometry(uint32_t vertexCount, uint32_t indexCount) override;

    bool isVrsSupported() const override;
    void finishRendering() override;
    void recreateOcclusionMesh(uint32_t viewIndex) override;

protected:
    void bindRenderTarget(const RenderTargetTextures& renderTarget) override;
    void unbindRenderTarget() override;
    void clearRenderTarget(const RenderTargetTextures& renderTarget, float r, float g, float b, float a) override;
    void freeCurrentRenderTarget() override;
    void useGeometry(const std::shared_ptr<Geometry>& geometry) override;
    void setupCamera(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;
    void setViewport(const varjo_Viewport& viewport) override;
    void updateVrsMap(const varjo_Viewport& viewport) override;
    void uploadInstanceBuffer(const std::vector<std::vector<ObjectRenderData>>& matrices) override;

    void drawGrid() override;
    void drawObjects(std::size_t objectsIndex) override;
    void drawMirrorWindow() override;
    void advance() override {}

    bool initVarjo() override;

    void createSwapchains() override;
    varjo_SwapChain* createSwapChain(varjo_SwapChainConfig2& swapchainConfig) override;
    void createMirrorWindow();
    varjo_ClipRange getClipRange() const override { return varjo_ClipRangeZeroToOne; }

    void postRenderView() override;
    void renderOcclusionMesh() override;

    void postRenderFrame() override;

private:
    static Microsoft::WRL::ComPtr<ID3D12Device2> createDevice(IDXGIAdapter4* adapter);
    static Microsoft::WRL::ComPtr<IDXGIAdapter4> getAdapter(varjo_Luid luid);
    Microsoft::WRL::ComPtr<ID3D12RootSignature> createRootSignature() const;
    DXGI_FORMAT getSwapchainNativeFormat() const;

    enum class BlendState { DISABLED, ENABLED };

    Microsoft::WRL::ComPtr<ID3D12PipelineState> createGridPipelineState(BlendState blendState, DXGI_FORMAT depthFormat) const;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> createOcclusionPipelineState(DXGI_FORMAT depthFormat) const;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> createDefaultPipelineState(DXGI_FORMAT depthFormat) const;
    Microsoft::WRL::ComPtr<ID3D12Resource> createUploadBuffer(size_t size) const;

    void renderOcclusionMesh(uint32_t viewIndex);
    GpuNode* gpuNodeForView(uint32_t viewIndex);

    static void upload(ID3D12Resource* buffer, size_t offset, const void* data, size_t size);
    static DXGI_FORMAT getSpecificDepthFormat(varjo_TextureFormat format);

    Microsoft::WRL::ComPtr<ID3D12Device2> m_device;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_gridPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_gridBlendEnabledPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_defaultPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_occlusionMeshState;

    bool m_initialized{false};

    struct ViewProjMatrix {
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec2 viewportSize;
    } m_viewProjMatrix{};

    D3D12_BOX m_currentViewportBox{};
    std::shared_ptr<Geometry> m_currentGeometry;  // Currently binded geometry with useGeometry() call

    bool m_useSli{false};
    int m_nodeCount{1};
    std::array<std::unique_ptr<GpuNode>, c_D3D12RenderingNodesInSli> m_gpuNodes;
    uint32_t m_sharedGpumask{0x1};
    std::unique_ptr<CrossNodeCopier> m_crossNodeCopier;

    RenderTargetTextures m_currentRenderTarget;

#ifdef D3D12_VRS_ENABLED
    void updateVrsMap(D3D12RenderTexture* vrsRenderTexture, ID3D12GraphicsCommandList5* commandList5, const varjo_Viewport& viewport);
    void drawVrsMap(D3D12RenderTexture* vrsRenderTexture, D3D12RenderTexture* colorRenderTexture, int nodeIndex);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> createVrsVisualizationPipelineState() const;

    UINT m_vrsTileSize{0};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_visualizeVrsPipelineState;
    std::shared_ptr<D3D12RenderTexture> m_vrsTexture;
#endif


    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_windowSwapChain;
};

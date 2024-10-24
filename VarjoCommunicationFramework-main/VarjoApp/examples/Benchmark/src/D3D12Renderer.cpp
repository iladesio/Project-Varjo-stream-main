#include "D3D12Renderer.hpp"

#include "Varjo_d3d12.h"
#include "Varjo_layers.h"

#include <d3dx12.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <glm/gtc/matrix_transform.inl>
#include <sstream>

#include "D3DShaders.hpp"
#include "VRSHelper.hpp"

// Application can't be profiled by PIX because Benchmark doesn't show any window.
// Uncommenting next line will use special functionality of D3D12 to pass render target to pix
// Also PIX doesn't implement 11On12 so it has to be disable from launching options (API dropdown)
// #define USE_PIX
using Microsoft::WRL::ComPtr;

namespace
{
constexpr int c_MaxInstances = 5000;
#define HCHECK(value) HCheck(#value, __LINE__, value)

void HCheck(const char* what, int line, HRESULT hr)
{
    if (FAILED(hr)) {
        printf("%s failed with code %d at line %d\n", what, hr, line);
        abort();
    }
}

static DXGI_FORMAT getDepthTextureSRVFormat(DXGI_FORMAT depthTextureFormat)
{
    switch (depthTextureFormat) {
        case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_D32_FLOAT;
        case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
        case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        default: assert(false && "Unknown depth format"); return DXGI_FORMAT_D32_FLOAT;
    }
}

#ifdef D3D12_VRS_ENABLED
UINT getVariableRateShadingTileSize(ID3D12Device* device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options{};
    device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options, sizeof(options));
    return options.ShadingRateImageTileSize;
}

void getVariableRateShadingTextureSize(ID3D12Device* device, UINT textureWidth, UINT textureHeight, UINT* vrsTextureWidth, UINT* vrsTextureHeight)
{
    const UINT tileSize = getVariableRateShadingTileSize(device);
    assert(tileSize);

    *vrsTextureWidth = static_cast<UINT>(ceil(static_cast<float>(textureWidth) / tileSize));
    *vrsTextureHeight = static_cast<UINT>(ceil(static_cast<float>(textureHeight) / tileSize));
}
#endif
}  // namespace

// Allocates given amount of descriptors on the heap and provides access to that
// via overloaded [] operator.
class DescriptorHeap final
{
public:
    DescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, uint32_t nodeMask)
        : m_count(count)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = count;
        heapDesc.Type = type;
        heapDesc.Flags = type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        heapDesc.NodeMask = nodeMask;
        HCHECK(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap)));
        m_descriptorHandleIncrementSize = device->GetDescriptorHandleIncrementSize(type);
        m_indexAvailable = std::vector<bool>(m_count, true);
    }

    Descriptor allocate()
    {
        bool availableIndexFound = false;
        size_t index = 0;
        for (size_t i = 0; i < m_indexAvailable.size(); ++i) {
            if (m_indexAvailable[i]) {
                index = i;
                availableIndexFound = true;
                m_indexAvailable[i] = false;
                break;
            }
        }
        assert(availableIndexFound);
        return at(static_cast<UINT>(index));
    }

    Descriptor at(UINT index) const
    {
        assert(index < m_count);
        Descriptor descriptor{};
        descriptor.cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_heap->GetCPUDescriptorHandleForHeapStart(), index, m_descriptorHandleIncrementSize);
        descriptor.gpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_heap->GetGPUDescriptorHandleForHeapStart(), index, m_descriptorHandleIncrementSize);
        descriptor.slotIndex = index;
        descriptor.allocated = true;
        return descriptor;
    }

    ComPtr<ID3D12DescriptorHeap> getNativeHeap() const { return m_heap; }

    void freeIndex(UINT index) { m_indexAvailable[index] = true; }

private:
    ComPtr<ID3D12DescriptorHeap> m_heap;
    const size_t m_count;
    std::vector<bool> m_indexAvailable;
    UINT m_descriptorHandleIncrementSize = 0;
};

class Texture2D
{
public:
    explicit Texture2D(const ComPtr<ID3D12Resource>& texture, IDescriptorAllocator* allocator)
        : m_texture(texture)
        , m_allocator(allocator)
    {
        m_texture->GetDevice(IID_PPV_ARGS(&m_device));
    }

    ~Texture2D()
    {
        if (m_allocator) {
            if (m_rtv.allocated) {
                m_allocator->getHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)->freeIndex(m_rtv.slotIndex);
            }
            if (m_dsv.allocated) {
                m_allocator->getHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV)->freeIndex(m_dsv.slotIndex);
            }
            if (m_uav.allocated) {
                m_allocator->getHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)->freeIndex(m_uav.slotIndex);
            }
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE createRtv(DXGI_FORMAT specificFormat = DXGI_FORMAT_UNKNOWN)
    {
        if (!m_rtv) {
            m_rtv = m_allocator->allocateFromHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Format = specificFormat;
            m_device->CreateRenderTargetView(m_texture.Get(), &rtvDesc, m_rtv);
        }

        return m_rtv;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE createDsv(DXGI_FORMAT specificFormat = DXGI_FORMAT_UNKNOWN)
    {
        if (!m_dsv) {
            m_dsv = m_allocator->allocateFromHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dsvDesc.Format = specificFormat;
            m_device->CreateDepthStencilView(m_texture.Get(), &dsvDesc, m_dsv);
        }

        return m_dsv;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE createUav(DXGI_FORMAT specificFormat = DXGI_FORMAT_UNKNOWN)
    {
        if (!m_uav) {
            m_uav = m_allocator->allocateFromHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Format = specificFormat;
            m_device->CreateUnorderedAccessView(m_texture.Get(), nullptr, &uavDesc, m_uav);
        }

        return m_uav;
    }

    Descriptor createSrv(DXGI_FORMAT specificFormat = DXGI_FORMAT_UNKNOWN)
    {
        if (!m_srv) {
            m_srv = m_allocator->allocateFromHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Format = specificFormat;
            m_device->CreateShaderResourceView(m_texture.Get(), &srvDesc, m_srv);
        }

        return m_srv;
    }

    const Descriptor& getRtv() const { return m_rtv; }
    const Descriptor& getDsv() const { return m_dsv; }
    const Descriptor& getUav() const { return m_uav; }

    ID3D12Resource* getNativeTexture() const { return m_texture.Get(); }
    operator ID3D12Resource*() const { return m_texture.Get(); }

private:
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_texture;
    IDescriptorAllocator* m_allocator{nullptr};
    Descriptor m_rtv;
    Descriptor m_dsv;
    Descriptor m_uav;
    Descriptor m_srv;
};

class ResourceBuilder
{
public:
    static ResourceBuilder Tex2D(DXGI_FORMAT format, UINT64 width, UINT height)
    {
        ResourceBuilder builder;
        builder.m_textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);
        builder.m_textureDesc.MipLevels = 1;
        builder.m_heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        return builder;
    }

    ResourceBuilder& withFlags(D3D12_RESOURCE_FLAGS flags)
    {
        m_textureDesc.Flags = flags;
        return *this;
    }

    ResourceBuilder& withClearValue(const D3D12_CLEAR_VALUE* clearValue)
    {
        m_clearValue = clearValue;
        return *this;
    }

    ResourceBuilder& onHeap(D3D12_HEAP_TYPE heapType)
    {
        m_heapProperties.Type = heapType;
        return *this;
    }

    ResourceBuilder& withNodeMask(UINT creationNodeMask, UINT visibleNodeMask)
    {
        m_heapProperties.CreationNodeMask = creationNodeMask;
        m_heapProperties.VisibleNodeMask = visibleNodeMask;
        return *this;
    }

    ResourceBuilder& withName(const std::wstring& name)
    {
        m_name = name;
        return *this;
    }

    ResourceBuilder& withInitialState(D3D12_RESOURCE_STATES initialState)
    {
        m_initialState = initialState;
        return *this;
    }

    std::shared_ptr<Texture2D> create(ID3D12Device* device, IDescriptorAllocator* allocator) const
    {
        ComPtr<ID3D12Resource> texture;
        HCHECK(device->CreateCommittedResource(&m_heapProperties, D3D12_HEAP_FLAG_NONE, &m_textureDesc, m_initialState, m_clearValue, IID_PPV_ARGS(&texture)));
        return std::make_shared<Texture2D>(texture, allocator);
    }

private:
    ResourceBuilder() = default;

    D3D12_RESOURCE_DESC m_textureDesc{};
    D3D12_HEAP_PROPERTIES m_heapProperties{};
    const D3D12_CLEAR_VALUE* m_clearValue{nullptr};
    std::wstring m_name;
    D3D12_RESOURCE_STATES m_initialState{D3D12_RESOURCE_STATE_COMMON};
};

// Encapsulates vertex and index buffers. Also uses intermediate buffers to upload
// initial data.
class D3D12GeometrySingleNode
{
public:
    D3D12GeometrySingleNode(
        D3D12Renderer* renderer, GpuNode* gpuNode, uint32_t vertexCount, uint32_t indexCount, uint32_t vertexDataSize, uint32_t indexDataSize)
        : m_vertexDataSize(vertexDataSize)
        , m_indexDataSize(indexDataSize)
        , m_nodeMask(gpuNode->nodeMask())
        , m_device(renderer->getDevice())
        , m_commandList(gpuNode->getCommandList())
    {
        m_vertexBuffer = createBuffer(vertexDataSize);
        m_vertexBuffer->SetName(L"Vertex Buffer");

        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.SizeInBytes = vertexDataSize;
        m_vertexBufferView.StrideInBytes = vertexDataSize / vertexCount;

        m_indexBuffer = createBuffer(indexDataSize);
        m_indexBuffer->SetName(L"Index Buffer");

        m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
        m_indexBufferView.SizeInBytes = indexDataSize;
        m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    }

    void updateVertexBuffer(void* data)
    {
        upload(m_commandList.Get(), m_vertexBuffer.Get(), data, m_vertexDataSize, m_vertexUploadBuffer.GetAddressOf());
        m_vertexUploadBuffer->SetName(L"Vertex Upload Buffer");
        D3D12_RESOURCE_BARRIER barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        m_commandList->ResourceBarrier(1, &barrier);
    }

    void updateIndexBuffer(void* data)
    {
        upload(m_commandList.Get(), m_indexBuffer.Get(), data, m_indexDataSize, m_indexUploadBuffer.GetAddressOf());
        m_indexUploadBuffer->SetName(L"Index Upload Buffer");
        D3D12_RESOURCE_BARRIER barrier =
            CD3DX12_RESOURCE_BARRIER::Transition(m_indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        m_commandList->ResourceBarrier(1, &barrier);
    }

    const D3D12_VERTEX_BUFFER_VIEW* getVertexBufferView() const { return &m_vertexBufferView; }
    const D3D12_INDEX_BUFFER_VIEW* getIndexBufferView() const { return &m_indexBufferView; }

private:
    ComPtr<ID3D12Resource> createBuffer(size_t size) const
    {
        CD3DX12_HEAP_PROPERTIES vertexProperties(D3D12_HEAP_TYPE_DEFAULT, m_nodeMask, m_nodeMask);
        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        ComPtr<ID3D12Resource> buffer;
        HCHECK(
            m_device->CreateCommittedResource(&vertexProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)));
        return buffer;
    }

    void upload(ID3D12GraphicsCommandList* commandList, ID3D12Resource* dest, const void* src, size_t size, ID3D12Resource** intermediateBuffer) const
    {
        CD3DX12_HEAP_PROPERTIES properties(D3D12_HEAP_TYPE_UPLOAD, m_nodeMask, m_nodeMask);
        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        HCHECK(m_device->CreateCommittedResource(
            &properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(intermediateBuffer)));

        D3D12_SUBRESOURCE_DATA subresourceData = {};
        subresourceData.pData = src;
        subresourceData.RowPitch = size;
        subresourceData.SlicePitch = subresourceData.RowPitch;

        UpdateSubresources(commandList, dest, *intermediateBuffer, 0, 0, 1, &subresourceData);
    }

    uint32_t m_vertexDataSize{0};
    uint32_t m_indexDataSize{0};
    uint32_t m_nodeMask{0x1};
    ComPtr<ID3D12Device2> m_device;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_vertexUploadBuffer;
    ComPtr<ID3D12Resource> m_indexUploadBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
};

class D3D12Geometry final : public Geometry
{
public:
    D3D12Geometry(D3D12Renderer* renderer, uint32_t vertexCount, uint32_t indexCount, bool useSli)
        : Geometry(vertexCount, indexCount)
    {
        for (int nodeIndex = 0; nodeIndex < c_D3D12RenderingNodesInSli; nodeIndex++) {
            if (!useSli && nodeIndex != 0) {
                m_geometry[nodeIndex] = nullptr;
            } else {
                m_geometry[nodeIndex] = std::make_unique<D3D12GeometrySingleNode>(
                    renderer, renderer->getGpuNode(nodeIndex), vertexCount, indexCount, getVertexDataSize(), getIndexDataSize());
            }
        }
    }

    void updateVertexBuffer(void* data) override
    {
        for (auto& geometry : m_geometry) {
            if (geometry) {
                geometry->updateVertexBuffer(data);
            }
        }
    }

    void updateIndexBuffer(void* data) override
    {
        for (auto& geometry : m_geometry) {
            if (geometry) {
                geometry->updateIndexBuffer(data);
            }
        }
    }

    const D3D12_VERTEX_BUFFER_VIEW* getVertexBufferView(uint32_t nodeIndex) const { return m_geometry[nodeIndex]->getVertexBufferView(); }
    const D3D12_INDEX_BUFFER_VIEW* getIndexBufferView(uint32_t nodeIndex) const { return m_geometry[nodeIndex]->getIndexBufferView(); }

private:
    std::array<std::unique_ptr<D3D12GeometrySingleNode>, c_D3D12RenderingNodesInSli> m_geometry;
};


CrossNodeCopier::CrossNodeCopier(const ComPtr<ID3D12Device2>& device, GpuNode* srcGpuNode, GpuNode* dstGpuNode, bool useDepthLayers, bool useVelocity)
    : m_srcGpuNode(srcGpuNode)
    , m_dstGpuNode(dstGpuNode)
    , m_useDepthLayers(useDepthLayers)
    , m_useVelocity(useVelocity)
{
    for (int i = 0; i < static_cast<int>(m_commandAllocators.size()); i++) {
        HCHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i])));
        std::wstringstream allocatorName;
        allocatorName << L"Post Command Allocator " << i;
        m_commandAllocators[i]->SetName(allocatorName.str().c_str());
    }
    HCHECK(device->CreateCommandList(
        dstGpuNode->nodeMask(), D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_allocatorIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
    m_commandList->SetName(L"Post Command List");

    HCHECK(m_commandList->Close());
    ID3D12CommandList* commandLists[] = {m_commandList.Get()};
    m_dstGpuNode->getCommandQueue()->ExecuteCommandLists(1, commandLists);
    m_allocatorIndex = (m_allocatorIndex + 1) % m_commandAllocators.size();
}

void CrossNodeCopier::copy(const RenderTargetTextures& renderTarget)
{
    const auto dxColorRenderTexture = std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(renderTarget.getColorTexture());
    const auto dxDepthRenderTexture = std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(renderTarget.getDepthTexture());
    const auto dxVelocityRenderTexture = std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(renderTarget.getVelocityTexture());

    auto nodeIndex = m_dstGpuNode->index();

    // wait first for source gpu to finish previous rendering, then execute the copy
    m_dstGpuNode->getCommandQueue()->Wait(m_srcGpuNode->fence().Get(), m_srcGpuNode->lastSignalledFenceValue());

    m_commandAllocators[m_allocatorIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_allocatorIndex].Get(), nullptr);

    D3D12_RESOURCE_BARRIER preCopyBarrier[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            dxColorRenderTexture->dxTexture(nodeIndex)->getNativeTexture(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
        CD3DX12_RESOURCE_BARRIER::Transition(
            dxColorRenderTexture->dxCrossNodeTexture(nodeIndex).Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
    };
    m_commandList->ResourceBarrier(2, preCopyBarrier);

    D3D12_TEXTURE_COPY_LOCATION dst{dxColorRenderTexture->dxTexture(nodeIndex)->getNativeTexture(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0};
    D3D12_TEXTURE_COPY_LOCATION src{dxColorRenderTexture->dxCrossNodeTexture(nodeIndex).Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0};

    for (auto box : m_viewportBoxes) {
        m_commandList->CopyTextureRegion(&dst, box.left, box.top, 0, &src, &box);
    }

    D3D12_RESOURCE_BARRIER postCopyBarrier[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(
            dxColorRenderTexture->dxTexture(nodeIndex)->getNativeTexture(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
        CD3DX12_RESOURCE_BARRIER::Transition(
            dxColorRenderTexture->dxCrossNodeTexture(nodeIndex).Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
    };
    m_commandList->ResourceBarrier(2, postCopyBarrier);

    if (m_useDepthLayers) {
        D3D12_RESOURCE_BARRIER preCopyBarrier[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                dxDepthRenderTexture->dxTexture(nodeIndex)->getNativeTexture(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(
                dxDepthRenderTexture->dxCrossNodeTexture(nodeIndex).Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
        };
        m_commandList->ResourceBarrier(2, preCopyBarrier);

        D3D12_TEXTURE_COPY_LOCATION dst{dxDepthRenderTexture->dxTexture(nodeIndex)->getNativeTexture(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0};
        D3D12_TEXTURE_COPY_LOCATION src{dxDepthRenderTexture->dxCrossNodeTexture(nodeIndex).Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0};

        for (auto box : m_viewportBoxes) {
            m_commandList->CopyTextureRegion(&dst, box.left, box.top, 0, &src, &box);
        }

        D3D12_RESOURCE_BARRIER postCopyBarrier[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                dxDepthRenderTexture->dxTexture(nodeIndex)->getNativeTexture(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
            CD3DX12_RESOURCE_BARRIER::Transition(
                dxDepthRenderTexture->dxCrossNodeTexture(nodeIndex).Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
        };
        m_commandList->ResourceBarrier(2, postCopyBarrier);
    }
    if (m_useVelocity) {
        D3D12_RESOURCE_BARRIER preCopyBarrier[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                dxVelocityRenderTexture->dxTexture(nodeIndex)->getNativeTexture(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(
                dxVelocityRenderTexture->dxCrossNodeTexture(nodeIndex).Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
        };
        m_commandList->ResourceBarrier(2, preCopyBarrier);

        D3D12_TEXTURE_COPY_LOCATION dst{dxVelocityRenderTexture->dxTexture(nodeIndex)->getNativeTexture(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0};
        D3D12_TEXTURE_COPY_LOCATION src{dxVelocityRenderTexture->dxCrossNodeTexture(nodeIndex).Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0};

        for (auto box : m_viewportBoxes) {
            m_commandList->CopyTextureRegion(&dst, box.left, box.top, 0, &src, &box);
        }

        D3D12_RESOURCE_BARRIER postCopyBarrier[2] = {
            CD3DX12_RESOURCE_BARRIER::Transition(
                dxVelocityRenderTexture->dxTexture(nodeIndex)->getNativeTexture(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
            CD3DX12_RESOURCE_BARRIER::Transition(
                dxVelocityRenderTexture->dxCrossNodeTexture(nodeIndex).Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
        };
        m_commandList->ResourceBarrier(2, postCopyBarrier);
    }

    m_viewportBoxes.clear();
    HCHECK(m_commandList->Close());
    ID3D12CommandList* commandLists[] = {m_commandList.Get()};
    m_dstGpuNode->getCommandQueue()->ExecuteCommandLists(1, commandLists);
    m_allocatorIndex = (m_allocatorIndex + 1) % m_commandAllocators.size();
}

GpuNode::GpuNode(uint32_t nodeIndex, const Microsoft::WRL::ComPtr<ID3D12Device2>& device, varjo_Session* session)
    : m_device(device)
    , m_nodeIndex(nodeIndex)
    , m_nodeMask(1 << nodeIndex)
{
    createResources(session);
}

GpuNode::~GpuNode() { waitForGpu(); }

void GpuNode::waitForGpu()
{
    HCHECK(m_queue->Signal(m_frameFence.Get(), m_frameNumber));
    HCHECK(m_frameFence->SetEventOnCompletion(m_frameNumber, m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    m_frameNumber++;
    updateFrameRingIndex();
}

Descriptor GpuNode::allocateFromHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) const { return getHeap(type)->allocate(); }

DescriptorHeap* GpuNode::getHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) const
{
    switch (type) {
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV: return m_rtvs.get();
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: return m_dsvs.get();
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: return m_uavs.get();
        default: assert(false && "Not supported");
    }

    return nullptr;
}

void GpuNode::completeFrameRender()
{
    /*
     Increasing frame number, signaling fence and recording that number to a perFrame structure. We could use those resources
     only when m_frameFence reaches that value. After that we update frame ring index.

     For the first frame it will look like this:
     m_frameNumber equals 0, so incrementing that will give us 1 - first frame.
     We add to the gpu queue, that m_frameFence has value 1.
     Store 1 to the fence value of the corresponding "per frame" resource structure
     Update frame ring index to 1 (m_frameNumber % c_D3D12RingBufferSize)

     When m_frameFence reaches value 1, m_perFrameResources[0] won't be used by GPU, and frame would be rendered completely.
     */
    m_frameNumber++;
    HCHECK(m_queue->Signal(m_frameFence.Get(), m_frameNumber));
    currentFrameResources().fenceValue = m_frameNumber;
    updateFrameRingIndex();
}

void GpuNode::createResources(varjo_Session* session)
{
    D3D12_COMMAND_QUEUE_DESC desc{D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, D3D12_COMMAND_QUEUE_FLAG_NONE, m_nodeMask};
    HCHECK(m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_queue)));
    std::wstringstream name;
    name << L"Direct Queue " << m_nodeMask;
    m_queue->SetName(name.str().c_str());

    const int maxNumberOfDescriptors = 20;
    m_rtvs = std::make_unique<DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, maxNumberOfDescriptors, m_nodeMask);
    name << L"RTV Heap " << m_nodeMask;
    m_rtvs->getNativeHeap()->SetName(name.str().c_str());

    m_dsvs = std::make_unique<DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, maxNumberOfDescriptors, m_nodeMask);
    name << L"DSV Heap " << m_nodeMask;
    m_dsvs->getNativeHeap()->SetName(name.str().c_str());

    m_uavs = std::make_unique<DescriptorHeap>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, maxNumberOfDescriptors, m_nodeMask);
    name << L"UAV Heap " << m_nodeMask;
    m_uavs->getNativeHeap()->SetName(name.str().c_str());

    for (int i = 0; i < c_D3D12RingBufferSize; i++) {
        HCHECK(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_perFrameResources[i].commandAllocator)));
        std::wstringstream allocatorName;
        allocatorName << L"Allocator " << i << "_" << m_nodeMask;
        m_perFrameResources[i].commandAllocator->SetName(allocatorName.str().c_str());
        m_perFrameResources[i].instanceBuffer = createUploadBuffer(c_MaxInstances * sizeof(IRenderer::ObjectRenderData));
        std::wstringstream instanceBufferName;
        instanceBufferName << L"Instance Buffer " << i << "_" << m_nodeMask;
        m_perFrameResources[i].instanceBuffer->SetName(instanceBufferName.str().c_str());
        m_perFrameResources[i].backBufferIndex = (i + 1) % c_D3D12RingBufferSize;
    }

    HCHECK(m_device->CreateCommandList(
        m_nodeMask, D3D12_COMMAND_LIST_TYPE_DIRECT, m_perFrameResources[m_frameRingIndex].commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
    name << L"Main Command List " << m_nodeMask;
    m_commandList->SetName(name.str().c_str());

    for (uint32_t viewIndex = 0; viewIndex < 2; viewIndex++) {
        createOcclusionMeshResources(session, viewIndex);
    }

    HCHECK(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_frameFence)));
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr) {
        HCHECK(HRESULT_FROM_WIN32(GetLastError()));
    }
}

ComPtr<ID3D12Resource> GpuNode::createUploadBuffer(size_t size) const
{
    CD3DX12_HEAP_PROPERTIES vertexProperties(D3D12_HEAP_TYPE_UPLOAD, m_nodeMask, m_nodeMask);
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size);
    ComPtr<ID3D12Resource> buffer;
    HCHECK(
        m_device->CreateCommittedResource(&vertexProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer)));
    return buffer;
}

void GpuNode::createOcclusionMeshResources(varjo_Session* session, uint32_t viewIndex)
{
    const ComPtr<ID3D12GraphicsCommandList> commandList = getCommandList();
    varjo_Mesh2Df* mesh = varjo_CreateOcclusionMesh(session, viewIndex, varjo_WindingOrder_CounterClockwise);
    m_occlusionMeshVertexCount[viewIndex] = mesh->vertexCount;

    if (mesh->vertexCount == 0) {
        return;
    }

    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT, m_nodeMask, m_nodeMask);
    const int32_t sizeInBytes = mesh->vertexCount * sizeof(varjo_Vector2Df);
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeInBytes);
    HCHECK(m_device->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_occlusionMeshBuffers[viewIndex])));
    m_occlusionMeshUploadBuffers[viewIndex] = createUploadBuffer(sizeInBytes);

    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = mesh->vertices;
    subresourceData.RowPitch = sizeInBytes;
    subresourceData.SlicePitch = subresourceData.RowPitch;

    UpdateSubresources(commandList.Get(), m_occlusionMeshBuffers[viewIndex].Get(), m_occlusionMeshUploadBuffers[viewIndex].Get(), 0, 0, 1, &subresourceData);
    varjo_FreeOcclusionMesh(mesh);
}

void GpuNode::updateFrameRingIndex()
{
    m_frameRingIndex = static_cast<int>(m_frameNumber % static_cast<int64_t>(c_D3D12RingBufferSize));  //
}

D3D12Renderer::D3D12Renderer(varjo_Session* session, const RendererSettings& rendererSettings)
    : IRenderer(session, rendererSettings)
{
#if defined(_DEBUG) && !defined(USE_PIX)  // PIX doesn't work with debug layer enabled
    ComPtr<ID3D12Debug> debugInterface;
    HCHECK(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
    debugInterface->EnableDebugLayer();

    ComPtr<ID3D12Debug1> debugInterface1;
    if (SUCCEEDED(debugInterface->QueryInterface(IID_PPV_ARGS(&debugInterface1)))) {
        debugInterface1->SetEnableGPUBasedValidation(true);
    }
#endif

    const ComPtr<IDXGIAdapter4> adapter = getAdapter(varjo_D3D11GetLuid(session));
    m_device = createDevice(adapter.Get());
    m_gpuNodes[0] = std::make_unique<GpuNode>(0, m_device, m_session);

    if (rendererSettings.useSli() && (m_device->GetNodeCount() >= c_D3D12RenderingNodesInSli)) {
        m_useSli = true;
        m_nodeCount = c_D3D12RenderingNodesInSli;
        for (int i = 0; i < c_D3D12RenderingNodesInSli; i++) {
            m_sharedGpumask |= 1 << i;
        }
        m_gpuNodes[1] = std::make_unique<GpuNode>(1, m_device, m_session);
        m_crossNodeCopier = std::make_unique<CrossNodeCopier>(m_device,  //
            m_gpuNodes[1].get(),                                         // src gpunode
            m_gpuNodes[0].get(),                                         // dst gpunode
            rendererSettings.useDepthLayers(), rendererSettings.useVelocity());
    }

    m_rootSignature = createRootSignature();
    m_rootSignature->SetName(L"Root Signature");

    const DXGI_FORMAT depthFormat = getSpecificDepthFormat(rendererSettings.depthFormat());
    m_defaultPipelineState = createDefaultPipelineState(depthFormat);
    m_defaultPipelineState->SetName(L"Default Pipeline State");
    m_gridPipelineState = createGridPipelineState(BlendState::DISABLED, depthFormat);
    m_gridPipelineState->SetName(L"Grid Pipeline State");
    m_gridBlendEnabledPipelineState = createGridPipelineState(BlendState::ENABLED, depthFormat);
    m_gridBlendEnabledPipelineState->SetName(L"Grid Blend Enabled Pipeline State");
    m_occlusionMeshState = createOcclusionPipelineState(depthFormat);
    m_occlusionMeshState->SetName(L"OcclusionMesh Pipeline State");

    if (rendererSettings.showMirrorWindow()) {
        createMirrorWindow();
    }

#ifdef D3D12_VRS_ENABLED
    if (m_settings.useVrs()) {
        m_vrsTileSize = getVariableRateShadingTileSize(m_device.Get());
        if (m_settings.visualizeVrs()) {
            m_visualizeVrsPipelineState = createVrsVisualizationPipelineState();
        }
    }
#endif
}

D3D12Renderer::~D3D12Renderer()
{
    freeRendererResources();  //
}

std::shared_ptr<RenderTexture> D3D12Renderer::createColorTexture(int32_t width, int32_t height, varjo_Texture colorTexture)
{
    const int maxNodeIndex = m_useSli ? 2 : 1;

#ifdef D3D12_VRS_ENABLED
    // Create VRS Texture
    if (m_settings.useVrs() && m_vrsTexture == nullptr) {
        std::array<std::unique_ptr<D3D12RenderTextureSingleNode>, c_D3D12RenderingNodesInSli> vrsTextureNodes;
        for (int nodeIndex = 0; nodeIndex < maxNodeIndex; nodeIndex++) {
            GpuNode* gpuNode = getGpuNode(nodeIndex);

            UINT vrsTextureWidth = 0, vrsTextureHeight = 0;
            getVariableRateShadingTextureSize(getDevice().Get(), width, height, &vrsTextureWidth, &vrsTextureHeight);

            std::shared_ptr<Texture2D> vrsTexture = ResourceBuilder::Tex2D(DXGI_FORMAT_R8_UINT, vrsTextureWidth, vrsTextureHeight)
                                                        .withFlags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
                                                        .withNodeMask(gpuNode->nodeMask(), getSharedGpuMask())
                                                        .withInitialState(D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE)
                                                        .create(getDevice().Get(), gpuNode);
            vrsTexture->createUav();

            vrsTextureNodes[nodeIndex] = std::make_unique<D3D12RenderTextureSingleNode>(vrsTexture);
            m_vrsTexture = std::make_shared<D3D12RenderTexture>(width, height, std::move(vrsTextureNodes));
        }
    }
#endif

    // Create textures on all nodes
    std::array<std::unique_ptr<D3D12RenderTextureSingleNode>, c_D3D12RenderingNodesInSli> textureNodes;
    ID3D12Resource* texture = varjo_ToD3D12Texture(colorTexture);
    for (int nodeIndex = 0; nodeIndex < maxNodeIndex; nodeIndex++) {
        GpuNode* gpuNode = getGpuNode(nodeIndex);
        bool canReuseTexture = false;
        if (texture != nullptr) {
            D3D12_HEAP_PROPERTIES heapProperties;
            texture->GetHeapProperties(&heapProperties, nullptr);
            canReuseTexture = gpuNode->nodeMask() == heapProperties.CreationNodeMask;
        }
        std::shared_ptr<Texture2D> newTexture;
        if (!canReuseTexture) {
            D3D12_CLEAR_VALUE clearValue{};
            clearValue.Format = getSwapchainNativeFormat();
            newTexture = ResourceBuilder::Tex2D(DXGI_FORMAT_R8G8B8A8_TYPELESS, width, height)
                             .withFlags(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
                             .withClearValue(&clearValue)
                             .withNodeMask(gpuNode->nodeMask(), getSharedGpuMask())
                             .create(getDevice().Get(), gpuNode);
        } else {
            newTexture = std::make_shared<Texture2D>(texture, gpuNode);
        }
        if (newTexture) {
            newTexture->createRtv(getSwapchainNativeFormat());
            newTexture->createUav(DXGI_FORMAT_R8G8B8A8_UNORM);
        }
        textureNodes[nodeIndex] = std::make_unique<D3D12RenderTextureSingleNode>(newTexture);
    }

    if (m_useSli) {
        // link above textures on main gpu by duplicating their handles
        textureNodes[0]->linkSharedResource(textureNodes[1].get());
    }

    return std::make_shared<D3D12RenderTexture>(width, height, std::move(textureNodes));
}
std::shared_ptr<RenderTexture> D3D12Renderer::createDepthTexture(int32_t width, int32_t height, varjo_Texture depthTexture)
{
    std::array<std::unique_ptr<D3D12RenderTextureSingleNode>, c_D3D12RenderingNodesInSli> textureNodes;
    ID3D12Resource* texture = varjo_ToD3D12Texture(depthTexture);

    // Create textures on all nodes
    const int maxNodeIndex = m_useSli ? 2 : 1;
    for (int nodeIndex = 0; nodeIndex < maxNodeIndex; nodeIndex++) {
        GpuNode* gpuNode = getGpuNode(nodeIndex);
        bool canReuseTexture = false;
        if (texture != nullptr) {
            D3D12_HEAP_PROPERTIES heapProperties;
            texture->GetHeapProperties(&heapProperties, nullptr);
            canReuseTexture = gpuNode->nodeMask() == heapProperties.CreationNodeMask;
        }
        std::shared_ptr<Texture2D> newTexture;
        if (!canReuseTexture) {
            D3D12_CLEAR_VALUE clearValue{};
            DXGI_FORMAT depthTextureFormat = getSpecificDepthFormat(m_settings.depthFormat());
            clearValue.Format = depthTextureFormat;
            clearValue.DepthStencil.Depth = 1.0f;

            newTexture = ResourceBuilder::Tex2D(depthTextureFormat, width, height)
                             .withFlags(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
                             .withClearValue(&clearValue)
                             .withNodeMask(gpuNode->nodeMask(), getSharedGpuMask())
                             .create(getDevice().Get(), gpuNode);
        } else {
            newTexture = std::make_shared<Texture2D>(texture, gpuNode);
        }
        if (newTexture) {
            const DXGI_FORMAT dsvFormat = getDepthTextureSRVFormat(newTexture->getNativeTexture()->GetDesc().Format);
            newTexture->createDsv(dsvFormat);
        }
        textureNodes[nodeIndex] = std::make_unique<D3D12RenderTextureSingleNode>(newTexture);
    }

    if (m_useSli) {
        // link above textures on main gpu by duplicating their handles
        textureNodes[0]->linkSharedResource(textureNodes[1].get());
    }

    return std::make_shared<D3D12RenderTexture>(width, height, std::move(textureNodes));
}
std::shared_ptr<RenderTexture> D3D12Renderer::createVelocityTexture(int32_t width, int32_t height, varjo_Texture velocityTexture)
{
    std::array<std::unique_ptr<D3D12RenderTextureSingleNode>, c_D3D12RenderingNodesInSli> textureNodes;
    ID3D12Resource* texture = varjo_ToD3D12Texture(velocityTexture);

    // Create textures on all nodes
    const int maxNodeIndex = m_useSli ? 2 : 1;
    for (int nodeIndex = 0; nodeIndex < maxNodeIndex; nodeIndex++) {
        GpuNode* gpuNode = getGpuNode(nodeIndex);
        std::shared_ptr<Texture2D> newTexture;
        if (texture != nullptr) {
            newTexture = std::make_shared<Texture2D>(texture, gpuNode);
            newTexture->createRtv(DXGI_FORMAT_R8G8B8A8_UINT);
        }
        textureNodes[nodeIndex] = std::make_unique<D3D12RenderTextureSingleNode>(newTexture);
    }

    if (m_useSli) {
        // link above textures on main gpu by duplicating their handles
        textureNodes[0]->linkSharedResource(textureNodes[1].get());
    }

    return std::make_shared<D3D12RenderTexture>(width, height, std::move(textureNodes));
}

std::shared_ptr<Geometry> D3D12Renderer::createGeometry(uint32_t vertexCount, uint32_t indexCount)
{
    return std::make_shared<D3D12Geometry>(this, vertexCount, indexCount, m_useSli);
}

#ifdef D3D12_VRS_ENABLED
bool D3D12Renderer::isVrsSupported() const
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options;
    return SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options, sizeof(options))) &&
           options.VariableShadingRateTier == D3D12_VARIABLE_SHADING_RATE_TIER_2;
}
#else
bool D3D12Renderer::isVrsSupported() const { return false; }
#endif

void D3D12Renderer::finishRendering()
{
    for (auto& node : m_gpuNodes) {
        node.reset();
    }
}

void D3D12Renderer::recreateOcclusionMesh(uint32_t viewIndex)
{
    if (m_settings.useOcclusionMesh() && viewIndex < 2) {
        for (int nodeIndex = 0; nodeIndex < m_nodeCount; nodeIndex++) {
            auto& node = m_gpuNodes[nodeIndex];
            node->waitForGpu();

            node->currentFrameResources().commandAllocator->Reset();
            node->getCommandList()->Reset(node->currentFrameResources().commandAllocator.Get(), nullptr);
            node->createOcclusionMeshResources(m_session, viewIndex);

            auto commandList = node->getCommandList();
            HCHECK(commandList->Close());
            ID3D12CommandList* commandLists[] = {commandList.Get()};
            node->getCommandQueue()->ExecuteCommandLists(1, commandLists);
            node->waitForGpu();
        }
    }
}

void D3D12Renderer::bindRenderTarget(const RenderTargetTextures& renderTarget)
{
    m_currentRenderTarget = renderTarget;

    if (!m_initialized) {
        m_initialized = true;
        for (int nodeIndex = 0; nodeIndex < m_nodeCount; nodeIndex++) {
            auto& node = m_gpuNodes[nodeIndex];
            auto commandList = node->getCommandList();
            HCHECK(commandList->Close());
            ID3D12CommandList* commandLists[] = {commandList.Get()};
            node->getCommandQueue()->ExecuteCommandLists(1, commandLists);
            node->waitForGpu();
        }
    }

    for (int nodeIndex = 0; nodeIndex < m_nodeCount; nodeIndex++) {
        auto& node = m_gpuNodes[nodeIndex];
        /*
         m_frameFence contains a value which will be changed by GPU. It's a monotonic number, increasing each frame.
         The contract for m_perFrameResources[m_frameRingIndex].fenceValue is such that when m_frameFence value equals
         to that, that means that all resources m_perFrameResources[m_frameRingIndex] are not used by GPU anymore.

         Varjo runtime allows one frame to be in flight and blocks varjo_EndFrameWithLayers when second frame cames too fast,
         so following condition never will be true and exist here only for illustrative purposes.
         */
        if (node->fence()->GetCompletedValue() < node->currentFrameResources().fenceValue) {
            HCHECK(node->fence()->SetEventOnCompletion(node->currentFrameResources().fenceValue, node->fenceEvent()));
            WaitForSingleObjectEx(node->fenceEvent(), INFINITE, FALSE);
        }

        node->currentFrameResources().commandAllocator->Reset();
        node->getCommandList()->Reset(node->currentFrameResources().commandAllocator.Get(), nullptr);

        const std::shared_ptr<D3D12RenderTexture> dxColorRenderTexture =
            std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(renderTarget.getColorTexture());
        const std::shared_ptr<D3D12RenderTexture> dxDepthRenderTexture =
            std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(renderTarget.getDepthTexture());
        const std::shared_ptr<D3D12RenderTexture> dxVelocityRenderTexture =
            std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(renderTarget.getVelocityTexture());

        D3D12_RESOURCE_BARRIER barrier[3] = {
            CD3DX12_RESOURCE_BARRIER::Transition(*dxColorRenderTexture->dxTexture(nodeIndex), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(*dxDepthRenderTexture->dxTexture(nodeIndex), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE),
            {},
        };
        D3D12_CPU_DESCRIPTOR_HANDLE renderTargers[2] = {dxColorRenderTexture->dxTexture(nodeIndex)->getRtv().cpuHandle, {}};
        UINT numBarriers = 2;
        UINT numRenderTargets = 1;

        if (dxVelocityRenderTexture) {
            const auto velocityTexture = dxVelocityRenderTexture->dxTexture(nodeIndex);
            if (velocityTexture) {
                numBarriers = 3;
                numRenderTargets = 2;
                barrier[2] = CD3DX12_RESOURCE_BARRIER::Transition(*velocityTexture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
                renderTargers[1] = velocityTexture->getRtv().cpuHandle;
            }
        }

        node->getCommandList()->ResourceBarrier(numBarriers, barrier);
        node->getCommandList()->OMSetRenderTargets(numRenderTargets, renderTargers, FALSE, &dxDepthRenderTexture->dxTexture(nodeIndex)->getDsv().cpuHandle);
    }
}

void D3D12Renderer::unbindRenderTarget()
{
    for (int nodeIndex = 0; nodeIndex < m_nodeCount; nodeIndex++) {
        auto& node = m_gpuNodes[nodeIndex];
        auto commandList = node->getCommandList();

        const std::shared_ptr<D3D12RenderTexture> dxColorRenderTexture =
            std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(m_currentRenderTarget.getColorTexture());
        const std::shared_ptr<D3D12RenderTexture> dxDepthRenderTexture =
            std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(m_currentRenderTarget.getDepthTexture());
        const std::shared_ptr<D3D12RenderTexture> dxVelocityRenderTexture =
            std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(m_currentRenderTarget.getVelocityTexture());

        D3D12_RESOURCE_BARRIER barrier[3] = {
            CD3DX12_RESOURCE_BARRIER::Transition(*dxColorRenderTexture->dxTexture(nodeIndex), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON),
            CD3DX12_RESOURCE_BARRIER::Transition(*dxDepthRenderTexture->dxTexture(nodeIndex), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COMMON),
            {},
        };
        UINT numBarriers = 2;

        if (dxVelocityRenderTexture) {
            const auto velocityTexture = dxVelocityRenderTexture->dxTexture(nodeIndex);
            if (velocityTexture) {
                barrier[2] = CD3DX12_RESOURCE_BARRIER::Transition(*velocityTexture, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
                numBarriers = 3;
            }
        }
        commandList->ResourceBarrier(numBarriers, barrier);

#ifdef D3D12_VRS_ENABLED
        if (m_settings.visualizeVrs()) {
            drawVrsMap(m_vrsTexture.get(), dxColorRenderTexture.get(), nodeIndex);
        }
#endif
        HCHECK(commandList->Close());

        ID3D12CommandList* commandLists[] = {commandList.Get()};
        node->getCommandQueue()->ExecuteCommandLists(1, commandLists);

        if (m_window) {
            m_window->present(m_windowSwapChain.Get());
        }

        node->completeFrameRender();

#ifdef USE_PIX
        ComPtr<ID3D12SharingContract> sharingContract;
        node->getCommandQueue().As(&sharingContract);
        node->waitForGpu();
        if (sharingContract) {
            const std::shared_ptr<D3D12RenderTarget> dxRenderTarget = std::static_pointer_cast<D3D12RenderTarget, RenderTarget>(m_currentRenderTarget);
            sharingContract->Present(dxRenderTarget->dxColorTexture(nodeIndex)->getNativeTexture(), 0, nullptr);
        }
#endif
    }
    if (m_useSli) {
        m_crossNodeCopier->copy(m_currentRenderTarget);
    }
}

void D3D12Renderer::clearRenderTarget(const RenderTargetTextures& renderTarget, float r, float g, float b, float a)
{
    const std::shared_ptr<D3D12RenderTexture> dxColorRenderTexture =
        std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(m_currentRenderTarget.getColorTexture());
    const std::shared_ptr<D3D12RenderTexture> dxDepthRenderTexture =
        std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(m_currentRenderTarget.getDepthTexture());
    const std::shared_ptr<D3D12RenderTexture> dxVelocityRenderTexture =
        std::static_pointer_cast<D3D12RenderTexture, RenderTexture>(m_currentRenderTarget.getVelocityTexture());

    FLOAT color[4] = {r, g, b, a};
    for (int nodeIndex = 0; nodeIndex < m_nodeCount; nodeIndex++) {
        auto& node = m_gpuNodes[nodeIndex];
        node->getCommandList()->ClearRenderTargetView(dxColorRenderTexture->dxTexture(nodeIndex)->getRtv(), color, 0, nullptr);
        node->getCommandList()->ClearDepthStencilView(dxDepthRenderTexture->dxTexture(nodeIndex)->getDsv(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
            m_settings.useReverseDepth() ? 0.0f : 1.0f, 0, 0, nullptr);
        if (dxVelocityRenderTexture && dxVelocityRenderTexture->dxTexture(nodeIndex)) {
            FLOAT zeroVelocity[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            node->getCommandList()->ClearRenderTargetView(dxVelocityRenderTexture->dxTexture(nodeIndex)->getRtv(), zeroVelocity, 0, nullptr);
        }
    }
}

void D3D12Renderer::freeCurrentRenderTarget() { m_currentRenderTarget.reset(); }

void D3D12Renderer::useGeometry(const std::shared_ptr<Geometry>& geometry) { m_currentGeometry = geometry; }

void D3D12Renderer::setupCamera(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
{
    m_viewProjMatrix.view = glm::transpose(viewMatrix);
    m_viewProjMatrix.proj = glm::transpose(projectionMatrix);
    m_viewProjMatrix.viewportSize = {m_currentViewportBox.right - m_currentViewportBox.left, m_currentViewportBox.bottom - m_currentViewportBox.top};
}

void D3D12Renderer::setViewport(const varjo_Viewport& viewport)
{
    ComPtr<ID3D12GraphicsCommandList> commandList = gpuNodeForView(m_currentViewIndex)->getCommandList();
    CD3DX12_VIEWPORT vp(                     //
        static_cast<FLOAT>(viewport.x),      //
        static_cast<FLOAT>(viewport.y),      //
        static_cast<FLOAT>(viewport.width),  //
        static_cast<FLOAT>(viewport.height)  //
    );

    commandList->RSSetViewports(1, &vp);
    CD3DX12_RECT scissor(0, 0, LONG_MAX, LONG_MAX);
    commandList->RSSetScissorRects(1, &scissor);

    m_currentViewportBox = {                              //
        static_cast<UINT>(viewport.x),                    //
        static_cast<UINT>(viewport.y),                    //
        0,                                                //
        static_cast<UINT>(viewport.x + viewport.width),   //
        static_cast<UINT>(viewport.y + viewport.height),  //
        1};
}

#ifndef D3D12_VRS_ENABLED
void D3D12Renderer::updateVrsMap(const varjo_Viewport& viewport) {}
#else
void D3D12Renderer::updateVrsMap(const varjo_Viewport& viewport)
{
    ComPtr<ID3D12GraphicsCommandList> commandList = gpuNodeForView(m_currentViewIndex)->getCommandList();
    ComPtr<ID3D12GraphicsCommandList5> commandList5;
    if (FAILED(commandList.As(&commandList5))) {
        abort();
    }
    D3D12RenderTexture* vrsRenderTexture = m_vrsTexture.get();

    const uint32_t nodeIndex = gpuNodeForView(m_currentViewIndex)->index();

    struct ID3D12Resource* vrsTexture = *vrsRenderTexture->dxTexture(nodeIndex);
    commandList5->ResourceBarrier(
        1, &CD3DX12_RESOURCE_BARRIER::Transition(vrsTexture, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    varjo_VariableRateShadingConfig config = getDefaultVRSConfig(m_currentViewIndex, viewport, m_vrsTileSize, m_settings, m_renderingGaze);

    varjo_D3D12UpdateVariableRateShadingTexture(m_session, commandList5.Get(), vrsTexture, &config);

    commandList5->ResourceBarrier(
        1, &CD3DX12_RESOURCE_BARRIER::Transition(vrsTexture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE));

    D3D12_SHADING_RATE_COMBINER chooseScreenspaceImage[2] = {
        D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_OVERRIDE};  // Choose screenspace image
    commandList5->RSSetShadingRate(D3D12_SHADING_RATE_4X4, chooseScreenspaceImage);
    commandList5->RSSetShadingRateImage(vrsTexture);
}

void D3D12Renderer::drawVrsMap(D3D12RenderTexture* vrsRenderTexture, D3D12RenderTexture* colorRenderTexture, int nodeIndex)
{
    struct VrsVisualizationConstants {
        float textureSize[2];
        float vrsMapSize[2];
    };

    auto& node = m_gpuNodes[nodeIndex];
    auto commandList = node->getCommandList();

    D3D12_RESOURCE_BARRIER barrier1[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(*colorRenderTexture->dxTexture(nodeIndex), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        CD3DX12_RESOURCE_BARRIER::Transition(
            *vrsRenderTexture->dxTexture(nodeIndex), D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
    };

    commandList->ResourceBarrier(2, barrier1);
    const D3D12_RESOURCE_DESC renderTargetDesc = colorRenderTexture->dxTexture(nodeIndex)->getNativeTexture()->GetDesc();
    // clang-format off
    VrsVisualizationConstants constants{
        {
            static_cast<float>(renderTargetDesc.Width),
            static_cast<float>(renderTargetDesc.Height)
        },
        {
            static_cast<float>(renderTargetDesc.Width / m_vrsTileSize),
            static_cast<float>(renderTargetDesc.Height / m_vrsTileSize)
        }
    };
    // clang-format on

    commandList->SetComputeRootSignature(m_rootSignature.Get());
    commandList->SetPipelineState(m_visualizeVrsPipelineState.Get());
    commandList->SetComputeRoot32BitConstants(0, sizeof(VrsVisualizationConstants) / 4, &constants, 0);
    ID3D12DescriptorHeap* heaps[] = {node->getHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)->getNativeHeap().Get()};
    commandList->SetDescriptorHeaps(1, heaps);
    const Descriptor baseDescriptor = colorRenderTexture->dxTexture(nodeIndex)->getUav();
    commandList->SetComputeRootDescriptorTable(1, baseDescriptor);
    const Descriptor descriptor = vrsRenderTexture->dxTexture(nodeIndex)->getUav();
    commandList->SetComputeRootDescriptorTable(2, descriptor);
    commandList->Dispatch(static_cast<UINT>(renderTargetDesc.Width) / 8, static_cast<UINT>(renderTargetDesc.Height) / 8, 1);

    D3D12_RESOURCE_BARRIER barrier2[2] = {
        CD3DX12_RESOURCE_BARRIER::Transition(*colorRenderTexture->dxTexture(nodeIndex), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
        CD3DX12_RESOURCE_BARRIER::Transition(
            *vrsRenderTexture->dxTexture(nodeIndex), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE),
    };

    commandList->ResourceBarrier(2, barrier2);
}

ComPtr<ID3D12PipelineState> D3D12Renderer::createVrsVisualizationPipelineState() const
{
    const ComPtr<ID3DBlob> computeShaderBlob = D3DShaders::compileVrsVisualizeShader();

    D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc{};
    computePipelineStateDesc.pRootSignature = m_rootSignature.Get();
    computePipelineStateDesc.CS = CD3DX12_SHADER_BYTECODE(computeShaderBlob->GetBufferPointer(), computeShaderBlob->GetBufferSize());

    ComPtr<ID3D12PipelineState> computeState;
    HCHECK(m_device->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&computeState)));
    return computeState;
}
#endif

void D3D12Renderer::renderOcclusionMesh(uint32_t viewIndex)
{
    const auto gpuNode = gpuNodeForView(viewIndex);

    if (gpuNode->getOcclusionMeshVertexCount(viewIndex) == 0) {
        return;
    }

    ComPtr<ID3D12GraphicsCommandList> commandList = gpuNode->getCommandList();
    commandList->SetPipelineState(m_occlusionMeshState.Get());
    commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VERTEX_BUFFER_VIEW view{                                                  //
        gpuNode->getOcclusionMeshGPUVirtualAddress(viewIndex),                      //
        gpuNode->getOcclusionMeshVertexCount(viewIndex) * sizeof(varjo_Vector2Df),  //
        sizeof(varjo_Vector2Df)};
    commandList->IASetVertexBuffers(0, 1, &view);
    commandList->DrawInstanced(gpuNode->getOcclusionMeshVertexCount(viewIndex), 1, 0, 0);
}

void D3D12Renderer::renderOcclusionMesh()
{
    if (m_settings.useOcclusionMesh() && m_currentViewIndex < 2) {
        renderOcclusionMesh(m_currentViewIndex);
    }
}

void D3D12Renderer::postRenderView()
{
    // if the view was not rendered on main gpu,
    // then after completing frame rendering, m_crossNodeCopier brings everything on main gpu.
    if (gpuNodeForView(m_currentViewIndex)->index() != 0) {
        m_crossNodeCopier->recordViewportBoxForCopy(m_currentViewportBox);
    }
}

void D3D12Renderer::drawGrid()
{
    auto node = gpuNodeForView(m_currentViewIndex);
    auto commandList = node->getCommandList();
    commandList->SetPipelineState(m_settings.useRenderVST() ? m_gridBlendEnabledPipelineState.Get() : m_gridPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(ViewProjMatrix) / 4, &m_viewProjMatrix, 0);

    const std::shared_ptr<D3D12Geometry> dxGeometry = std::static_pointer_cast<D3D12Geometry, Geometry>(m_currentGeometry);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, dxGeometry->getVertexBufferView(node->index()));
    commandList->IASetIndexBuffer(dxGeometry->getIndexBufferView(node->index()));

    commandList->DrawIndexedInstanced(m_currentGeometry->indexCount(), 1, 0, 0, 0);
}

void D3D12Renderer::uploadInstanceBuffer(const std::vector<std::vector<ObjectRenderData>>& matrices)
{
    for (int nodeIndex = 0; nodeIndex < m_nodeCount; nodeIndex++) {
        auto& node = m_gpuNodes[nodeIndex];
        auto& frameResources = node->currentFrameResources();
        frameResources.instancedObjectsOffsetCount.resize(0);
        frameResources.instancedObjectsOffsetCount.reserve(matrices.size());
        std::size_t dataSize = 0;
        for (const auto& singleDrawMatrices : matrices) {
            dataSize += singleDrawMatrices.size();
        }
        std::vector<ObjectRenderData> instanceBufferData;
        instanceBufferData.reserve(dataSize);
        std::size_t dataOffset = 0;
        for (const auto& singleDrawMatrices : matrices) {
            frameResources.instancedObjectsOffsetCount.push_back(std::make_pair(dataOffset, singleDrawMatrices.size()));
            dataOffset += singleDrawMatrices.size() * sizeof(ObjectRenderData);
            instanceBufferData.insert(instanceBufferData.end(), singleDrawMatrices.begin(), singleDrawMatrices.end());
        }
        upload(frameResources.instanceBuffer.Get(), 0, reinterpret_cast<const void*>(instanceBufferData.data()),
            instanceBufferData.size() * sizeof(ObjectRenderData));
    }
}

void D3D12Renderer::drawObjects(std::size_t objectsIndex)
{
    auto node = gpuNodeForView(m_currentViewIndex);
    ComPtr<ID3D12GraphicsCommandList> commandList = node->getCommandList();

    commandList->SetPipelineState(m_defaultPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    commandList->SetGraphicsRoot32BitConstants(0, sizeof(ViewProjMatrix) / 4, &m_viewProjMatrix, 0);

    const std::shared_ptr<D3D12Geometry> dxGeometry = std::static_pointer_cast<D3D12Geometry, Geometry>(m_currentGeometry);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const auto& frameResources = node->currentFrameResources();
    const auto& drawOffsetCount = frameResources.instancedObjectsOffsetCount[objectsIndex];
    D3D12_VERTEX_BUFFER_VIEW vertexBuffers[2] = {*dxGeometry->getVertexBufferView(node->index()), {}};
    vertexBuffers[1].BufferLocation = frameResources.instanceBuffer->GetGPUVirtualAddress() + drawOffsetCount.first;
    vertexBuffers[1].SizeInBytes = static_cast<UINT>(drawOffsetCount.second * sizeof(IRenderer::ObjectRenderData));
    vertexBuffers[1].StrideInBytes = sizeof(IRenderer::ObjectRenderData);

    commandList->IASetVertexBuffers(0, _countof(vertexBuffers), vertexBuffers);
    commandList->IASetIndexBuffer(dxGeometry->getIndexBufferView(node->index()));
    commandList->DrawIndexedInstanced(m_currentGeometry->indexCount(), static_cast<UINT>(drawOffsetCount.second), 0, 0, 0);
}

void D3D12Renderer::drawMirrorWindow()
{
    int32_t index;
    varjo_AcquireSwapChainImage(m_mirrorSwapchain, &index);
    if (varjo_GetError(m_session) == varjo_NoError) {
        const varjo_Texture swapchainTexture = varjo_GetSwapChainImage(m_mirrorSwapchain, index);
        ID3D12Resource* source = varjo_ToD3D12Texture(swapchainTexture);

        Microsoft::WRL::ComPtr<IDXGISwapChain3> windowSwapChain3;
        m_windowSwapChain.As(&windowSwapChain3);
        Microsoft::WRL::ComPtr<ID3D12Resource> destination;
        m_windowSwapChain->GetBuffer(m_gpuNodes[0]->currentFrameResources().backBufferIndex, IID_PPV_ARGS(&destination));

        for (int i = 0; i < 2; i++) {
            const varjo_MirrorView& view = m_mirrorViews[i];
            D3D12_BOX copyBox;
            copyBox.front = 0;
            copyBox.back = 1;
            copyBox.left = view.viewport.x;
            copyBox.top = view.viewport.y;
            copyBox.right = view.viewport.x + view.viewport.width;
            copyBox.bottom = view.viewport.y + view.viewport.height;

            D3D12_TEXTURE_COPY_LOCATION dstLocation{};
            dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstLocation.pResource = destination.Get();
            dstLocation.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION srcLocation{};
            srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            srcLocation.pResource = source;
            srcLocation.SubresourceIndex = 0;

            m_gpuNodes[0]->getCommandList()->ResourceBarrier(
                1, &CD3DX12_RESOURCE_BARRIER::Transition(destination.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST));
            m_gpuNodes[0]->getCommandList()->CopyTextureRegion(&dstLocation, view.viewport.x, view.viewport.y, 0, &srcLocation, &copyBox);
            m_gpuNodes[0]->getCommandList()->ResourceBarrier(
                1, &CD3DX12_RESOURCE_BARRIER::Transition(destination.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT));
        }
        varjo_ReleaseSwapChainImage(m_mirrorSwapchain);
    }
}

void D3D12Renderer::postRenderFrame() {}


bool D3D12Renderer::initVarjo()
{
    createSwapchains();

    // Check if the initialization was successful.
    const varjo_Error error = varjo_GetError(m_session);
    if (error != varjo_NoError) {
        printf(varjo_GetErrorDesc(error));
        return false;
    }

    return true;
}

void D3D12Renderer::createMirrorWindow()
{
    glm::ivec2 windowSize = getMirrorWindowSize();
    m_window = std::make_unique<Window>(windowSize.x, windowSize.y, false);

    ComPtr<IDXGIFactory2> dxgiFactory = nullptr;
    UINT createFactoryFlags = 0;
    const HRESULT hr = CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Width = m_window->getWidth();
    swapChainDesc.Height = m_window->getHeight();
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = false;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.Scaling = DXGI_SCALING_NONE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = 0;

    HCHECK(dxgiFactory->CreateSwapChainForHwnd(
        m_gpuNodes[0]->getCommandQueue().Get(), m_window->getHandle(), &swapChainDesc, nullptr, nullptr, m_windowSwapChain.GetAddressOf()));
}

varjo_SwapChain* D3D12Renderer::createSwapChain(varjo_SwapChainConfig2& swapchainConfig)
{
    return varjo_D3D12CreateSwapChain(m_session, m_gpuNodes[0]->getCommandQueue().Get(), &swapchainConfig);
}

void D3D12Renderer::createSwapchains()
{
    // create color texture swap chain
    m_swapChainConfig.numberOfTextures = 3;
    m_swapChainConfig.textureArraySize = 1;
    m_swapChainConfig.textureFormat = m_settings.noSrgb() ? varjo_TextureFormat_R8G8B8A8_UNORM : varjo_TextureFormat_R8G8B8A8_SRGB;
    m_swapChainConfig.textureWidth = getTotalViewportsWidth();
    m_swapChainConfig.textureHeight = getTotalViewportsHeight();

    m_colorSwapChain = varjo_D3D12CreateSwapChain(m_session, m_gpuNodes[0]->getCommandQueue().Get(), &m_swapChainConfig);

    if (m_settings.useDepthLayers()) {
        m_depthSwapChainConfig = m_swapChainConfig;
        m_depthSwapChainConfig.textureFormat = m_settings.depthFormat();
        m_depthSwapChain = varjo_D3D12CreateSwapChain(m_session, m_gpuNodes[0]->getCommandQueue().Get(), &m_depthSwapChainConfig);
    }
    if (m_settings.useVelocity()) {
        m_velocitySwapChainConfig = m_swapChainConfig;
        m_velocitySwapChainConfig.textureFormat = varjo_VelocityTextureFormat_R8G8B8A8_UINT;
        m_velocitySwapChain = varjo_D3D12CreateSwapChain(m_session, m_gpuNodes[0]->getCommandQueue().Get(), &m_velocitySwapChainConfig);
    }
}

ComPtr<IDXGIAdapter4> D3D12Renderer::getAdapter(varjo_Luid luid)
{
    ComPtr<IDXGIFactory> factory = nullptr;
    UINT createFactoryFlags = 0;
#if defined(_DEBUG) && !defined(USE_PIX)
    createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    const HRESULT hr = CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        UINT i = 0;
        while (true) {
            ComPtr<IDXGIAdapter> adapter = nullptr;
            if (factory->EnumAdapters(i++, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(adapter->GetDesc(&desc)) && desc.AdapterLuid.HighPart == luid.high && desc.AdapterLuid.LowPart == luid.low) {
                ComPtr<IDXGIAdapter4> adapter4;
                adapter.As(&adapter4);
                return adapter4;
            }
        }
    }
    return nullptr;
}

ComPtr<ID3D12Device2> D3D12Renderer::createDevice(IDXGIAdapter4* adapter)
{
    ComPtr<ID3D12Device2> d3d12Device2;
    HCHECK(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));

#if defined(_DEBUG) && !defined(USE_PIX)
    ComPtr<ID3D12InfoQueue> pInfoQueue;
    if (SUCCEEDED(d3d12Device2.As(&pInfoQueue))) {
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

        // Suppress messages based on their severity level
        D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO};

        // Suppress individual messages by their ID
        D3D12_MESSAGE_ID denyIds[] = {
            D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE, D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE, D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
            D3D12_MESSAGE_ID_REFLECTSHAREDPROPERTIES_INVALIDOBJECT  // CreateWrappedResource will show this error, can be ignored
        };

        D3D12_INFO_QUEUE_FILTER newFilter = {};
        newFilter.DenyList.NumSeverities = _countof(severities);
        newFilter.DenyList.pSeverityList = severities;
        newFilter.DenyList.NumIDs = _countof(denyIds);
        newFilter.DenyList.pIDList = denyIds;

        HCHECK(pInfoQueue->PushStorageFilter(&newFilter));
    }
#endif
    return d3d12Device2;
}

ComPtr<ID3D12RootSignature> D3D12Renderer::createRootSignature() const
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData{};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData)))) {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Allow input layout and deny unnecessary access to certain pipeline stages.
    const D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

    CD3DX12_ROOT_PARAMETER1 rootParameters[3]{};
    // 2 matrices: view and projection
    rootParameters[0].InitAsConstants((2 * sizeof(DirectX::XMMATRIX) + sizeof(DirectX::XMFLOAT2)) / 4, 0, 0, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_DESCRIPTOR_RANGE1 ranges1[1]{};
    ranges1[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    rootParameters[1].InitAsDescriptorTable(_countof(ranges1), ranges1);

    CD3DX12_DESCRIPTOR_RANGE1 ranges2[1]{};
    ranges2[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    rootParameters[2].InitAsDescriptorTable(_countof(ranges2), ranges2);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
    rootSignatureDescription.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

    ComPtr<ID3DBlob> rootSignatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    const HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDescription, featureData.HighestVersion, &rootSignatureBlob, &errorBlob);
    if (FAILED(hr)) {
        printf("D3DX12SerializeVersionedRootSignature failed: %x\nError:%s", hr, static_cast<const char*>(errorBlob->GetBufferPointer()));
    }

    // Create the root signature.
    ComPtr<ID3D12RootSignature> rootSignature;
    HCHECK(m_device->CreateRootSignature(
        m_sharedGpumask, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
    return rootSignature;
}

DXGI_FORMAT D3D12Renderer::getSwapchainNativeFormat() const { return m_settings.noSrgb() ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; }

ComPtr<ID3D12PipelineState> D3D12Renderer::createGridPipelineState(BlendState blendState, DXGI_FORMAT depthFormat) const
{
    const ComPtr<ID3DBlob> vertexShaderBlob = D3DShaders::compileGridVertexShader();
    const ComPtr<ID3DBlob> pixelShaderBlob = D3DShaders::compileGridPixelShader();

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    struct PipelineStateStream {
        CD3DX12_PIPELINE_STATE_STREAM_NODE_MASK nodeMask;
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT inputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS vs;
        CD3DX12_PIPELINE_STATE_STREAM_PS ps;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsvFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtvFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depthStencilState;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC blendState;
    } pipelineStateStream;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = getSwapchainNativeFormat();

    pipelineStateStream.nodeMask = m_sharedGpumask;
    pipelineStateStream.rootSignature = m_rootSignature.Get();
    pipelineStateStream.inputLayout = {inputLayout, _countof(inputLayout)};
    pipelineStateStream.primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.vs = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
    pipelineStateStream.ps = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
    pipelineStateStream.dsvFormat = depthFormat;
    pipelineStateStream.rtvFormats = rtvFormats;

    // Grid doesn't write to depth buffer, donuts will be rendered on top of it,
    // however stencil test is enabled
    CD3DX12_DEPTH_STENCIL_DESC depthStateDesc(D3D12_DEFAULT);
    depthStateDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStateDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    depthStateDesc.StencilEnable = m_settings.useOcclusionMesh();
    // Make stencil buffer read only
    depthStateDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthStateDesc.StencilWriteMask = 0;

    // Stencil test is passed is when value from stencil buffer equals to 0 (occlusion mask changed stencil value to 1)
    D3D12_DEPTH_STENCILOP_DESC stencilDesc;
    stencilDesc.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    stencilDesc.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    stencilDesc.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    stencilDesc.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

    depthStateDesc.FrontFace = stencilDesc;
    depthStateDesc.BackFace = stencilDesc;

    pipelineStateStream.depthStencilState = depthStateDesc;

    CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    pipelineStateStream.rasterizer = rasterizerDesc;

    CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
    blendDesc.RenderTarget[0].BlendEnable = blendState == BlendState::ENABLED;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipelineStateStream.blendState = blendDesc;

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {sizeof(PipelineStateStream), &pipelineStateStream};
    ComPtr<ID3D12PipelineState> gridPipelineState;
    HCHECK(m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&gridPipelineState)));
    return gridPipelineState;
}

ComPtr<ID3D12PipelineState> D3D12Renderer::createOcclusionPipelineState(DXGI_FORMAT depthFormat) const
{
    const ComPtr<ID3DBlob> vertexShaderBlob = D3DShaders::compileOcclusionVertexShader();
    const ComPtr<ID3DBlob> pixelShaderBlob = D3DShaders::compileOcclusionPixelShader();

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    struct PipelineStateStream {
        CD3DX12_PIPELINE_STATE_STREAM_NODE_MASK nodeMask;
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT inputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS vs;
        CD3DX12_PIPELINE_STATE_STREAM_PS ps;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsvFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtvFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depthStencilState;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
    } pipelineStateStream;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = getSwapchainNativeFormat();

    pipelineStateStream.nodeMask = m_sharedGpumask;
    pipelineStateStream.rootSignature = m_rootSignature.Get();
    pipelineStateStream.inputLayout = {inputLayout, _countof(inputLayout)};
    pipelineStateStream.primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.vs = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
    pipelineStateStream.ps = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
    pipelineStateStream.dsvFormat = depthFormat;
    pipelineStateStream.rtvFormats = rtvFormats;

    CD3DX12_DEPTH_STENCIL_DESC depthStateDesc(D3D12_DEFAULT);
    // Disable depth for stencil mask
    depthStateDesc.DepthEnable = false;
    depthStateDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStateDesc.DepthFunc = m_settings.useReverseDepth() ? D3D12_COMPARISON_FUNC_GREATER : D3D12_COMPARISON_FUNC_LESS;

    // Stencil test parameters
    depthStateDesc.StencilEnable = true;
    depthStateDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthStateDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

    D3D12_DEPTH_STENCILOP_DESC stencilDesc;
    stencilDesc.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    stencilDesc.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    // Increase stencil value by 1 (basically write 1 to stencil buffer where occlusion mask is)
    stencilDesc.StencilPassOp = D3D12_STENCIL_OP_INCR;
    stencilDesc.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    depthStateDesc.FrontFace = stencilDesc;
    depthStateDesc.BackFace = stencilDesc;

    pipelineStateStream.depthStencilState = depthStateDesc;

    CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);
    rasterizerDesc.CullMode = D3D12_CULL_MODE_FRONT;
    pipelineStateStream.rasterizer = rasterizerDesc;

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {sizeof(PipelineStateStream), &pipelineStateStream};
    ComPtr<ID3D12PipelineState> pipelineState;
    HCHECK(m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&pipelineState)));
    return pipelineState;
}

ComPtr<ID3D12PipelineState> D3D12Renderer::createDefaultPipelineState(DXGI_FORMAT depthFormat) const
{
    const ComPtr<ID3DBlob> vertexShaderBlob = D3DShaders::compileDefaultVertexShader(m_settings);
    const ComPtr<ID3DBlob> pixelShaderBlob = D3DShaders::compileDefaultPixelShader(m_settings);

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 7, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    };

    struct PipelineStateStream {
        CD3DX12_PIPELINE_STATE_STREAM_NODE_MASK nodeMask;
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT inputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primitiveTopologyType;
        CD3DX12_PIPELINE_STATE_STREAM_VS vs;
        CD3DX12_PIPELINE_STATE_STREAM_PS ps;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsvFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtvFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depthStencilState;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
    } pipelineStateStream;

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 2;
    rtvFormats.RTFormats[0] = getSwapchainNativeFormat();
    rtvFormats.RTFormats[1] = DXGI_FORMAT_R8G8B8A8_UINT;

    pipelineStateStream.nodeMask = m_sharedGpumask;
    pipelineStateStream.rootSignature = m_rootSignature.Get();
    pipelineStateStream.inputLayout = {inputLayout, _countof(inputLayout)};
    pipelineStateStream.primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.vs = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
    pipelineStateStream.ps = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
    pipelineStateStream.dsvFormat = depthFormat;
    pipelineStateStream.rtvFormats = rtvFormats;

    CD3DX12_DEPTH_STENCIL_DESC depthStateDesc(D3D12_DEFAULT);
    depthStateDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStateDesc.DepthFunc = m_settings.useReverseDepth() ? D3D12_COMPARISON_FUNC_GREATER : D3D12_COMPARISON_FUNC_LESS;

    depthStateDesc.StencilEnable = m_settings.useOcclusionMesh();
    // Make stencil buffer read only
    depthStateDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthStateDesc.StencilWriteMask = 0;

    // Stencil test is passed is when value from stencil buffer equals to 0 (occlusion mask changed stencil value to 1)
    D3D12_DEPTH_STENCILOP_DESC stencilDesc;
    stencilDesc.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    stencilDesc.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    stencilDesc.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    stencilDesc.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

    depthStateDesc.FrontFace = stencilDesc;
    depthStateDesc.BackFace = stencilDesc;

    pipelineStateStream.depthStencilState = depthStateDesc;

    CD3DX12_RASTERIZER_DESC rasterizerDesc(D3D12_DEFAULT);
    rasterizerDesc.CullMode = D3D12_CULL_MODE_FRONT;
    pipelineStateStream.rasterizer = rasterizerDesc;

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {sizeof(PipelineStateStream), &pipelineStateStream};
    ComPtr<ID3D12PipelineState> pipelineState;
    HCHECK(m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&pipelineState)));
    return pipelineState;
}

ComPtr<ID3D12Resource> D3D12Renderer::createUploadBuffer(size_t size) const
{
    CD3DX12_HEAP_PROPERTIES vertexProperties(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(size);
    ComPtr<ID3D12Resource> buffer;
    HCHECK(
        m_device->CreateCommittedResource(&vertexProperties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer)));
    return buffer;
}

void D3D12Renderer::upload(ID3D12Resource* buffer, size_t offset, const void* data, size_t size)
{
    void* gpuMem;
    CD3DX12_RANGE range(offset, offset + size);
    HCHECK(buffer->Map(0, &range, &gpuMem));
    void* gpuMemOffset = reinterpret_cast<void*>(reinterpret_cast<char*>(gpuMem) + offset);
    memcpy(gpuMemOffset, data, size);
    buffer->Unmap(0, &range);
}

DXGI_FORMAT D3D12Renderer::getSpecificDepthFormat(varjo_TextureFormat format)
{
    switch (format) {
        case varjo_DepthTextureFormat_D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
        case varjo_DepthTextureFormat_D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case varjo_DepthTextureFormat_D32_FLOAT_S8_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        default: assert(false && "Unknown depth format"); return DXGI_FORMAT_D32_FLOAT;
    }
}

GpuNode* D3D12Renderer::gpuNodeForView(uint32_t viewIndex)
{
    if (m_useSli) {
        if (m_settings.useSlaveGpu()) {
            // render all views on slave gpu
            return m_gpuNodes[1].get();
        }

        if (viewIndex == 1 || viewIndex == 3) {
            // render right-eye on gpu1
            return m_gpuNodes[1].get();
        } else if (viewIndex == 0 || viewIndex == 2) {
            // render left-eye on gpu0
            return m_gpuNodes[0].get();
        }
    }
    return m_gpuNodes[0].get();
}

D3D12RenderTextureSingleNode::D3D12RenderTextureSingleNode(std::shared_ptr<Texture2D> texture)
    : m_Texture(texture)
{
}

void D3D12RenderTextureSingleNode::linkSharedResource(D3D12RenderTextureSingleNode* otherNodeRenderTarget)
{
    if (otherNodeRenderTarget) otherNodeRenderTarget->dxTexture()->getNativeTexture()->QueryInterface(IID_PPV_ARGS(&m_crossNodeTexture));
}

D3D12RenderTexture::D3D12RenderTexture(
    int width, int height, std::array<std::unique_ptr<D3D12RenderTextureSingleNode>, c_D3D12RenderingNodesInSli>&& renderTextures)
    : RenderTexture(width, height)
    , m_renderTextures(std::move(renderTextures))
{
}

varjo_Texture D3D12RenderTexture::texture() const
{
    return varjo_FromD3D12Texture(m_renderTextures[0]->dxTexture()->getNativeTexture());  //
}

std::shared_ptr<Texture2D> D3D12RenderTexture::dxTexture(uint32_t nodeIndex) const
{
    return m_renderTextures[nodeIndex]->dxTexture();  //
}

Microsoft::WRL::ComPtr<ID3D12Resource> D3D12RenderTexture::dxCrossNodeTexture(uint32_t nodeIndex) const
{
    return m_renderTextures[nodeIndex]->dxCrossNodeTexture();
}

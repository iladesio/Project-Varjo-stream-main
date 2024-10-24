#include <cstdio>
#include <glm/gtc/type_ptr.hpp>
#include <Varjo_layers.h>
#include "D3DShaders.hpp"
#include "D3D11Renderer.hpp"
#include "VRSHelper.hpp"

using Microsoft::WRL::ComPtr;

namespace
{
#ifdef NVAPI_ENABLED
const NV_PIXEL_SHADING_RATE c_nvShadingRates[c_shadingRateCount] = {
    NV_PIXEL_X16_PER_RASTER_PIXEL,      //
    NV_PIXEL_X8_PER_RASTER_PIXEL,       //
    NV_PIXEL_X4_PER_RASTER_PIXEL,       //
    NV_PIXEL_X2_PER_RASTER_PIXEL,       //
    NV_PIXEL_X1_PER_RASTER_PIXEL,       //
    NV_PIXEL_X1_PER_1X2_RASTER_PIXELS,  //
    NV_PIXEL_X1_PER_2X1_RASTER_PIXELS,  //
    NV_PIXEL_X1_PER_2X2_RASTER_PIXELS,  //
    NV_PIXEL_X1_PER_2X4_RASTER_PIXELS,  //
    NV_PIXEL_X1_PER_4X2_RASTER_PIXELS,  //
    NV_PIXEL_X1_PER_4X4_RASTER_PIXELS,  //
    NV_PIXEL_X0_CULL_RASTER_PIXELS,     //
    NV_PIXEL_X0_CULL_RASTER_PIXELS,     //
    NV_PIXEL_X0_CULL_RASTER_PIXELS,     //
    NV_PIXEL_X0_CULL_RASTER_PIXELS,     //
    NV_PIXEL_X0_CULL_RASTER_PIXELS      //
};
#endif

#define HCHECK(value) HCheck(#value, __LINE__, value)

void HCheck(const char* what, int line, HRESULT hr)
{
    if (FAILED(hr)) {
        printf("%s failed with code %d at line %d\n", what, hr, line);
        abort();
    }
}
}  // namespace

ComPtr<IDXGIAdapter> getAdapter(varjo_Luid luid)
{
    ComPtr<IDXGIFactory> factory = nullptr;

    const HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        UINT i = 0;
        while (true) {
            ComPtr<IDXGIAdapter> adapter = nullptr;
            if (factory->EnumAdapters(i++, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(adapter->GetDesc(&desc)) && desc.AdapterLuid.HighPart == luid.high && desc.AdapterLuid.LowPart == luid.low) {
                return adapter;
            }
        }
    }
    return nullptr;
}

D3D11ColorRenderTexture::D3D11ColorRenderTexture(
    D3D11Renderer* renderer, int32_t width, int32_t height, ID3D11Texture2D* texture, DXGI_FORMAT rtvFormat, DXGI_FORMAT uavFormat)
    : RenderTexture(width, height)
{
    if (texture) texture->AddRef();
    createRtvAndUav(renderer, texture, rtvFormat, uavFormat);
}

D3D11ColorRenderTexture::~D3D11ColorRenderTexture()
{
    m_unorderedAccessView->Release();
    m_renderTargetView->Release();
    m_texture->Release();
}

void D3D11ColorRenderTexture::createRtvAndUav(D3D11Renderer* renderer, ID3D11Texture2D* texture, DXGI_FORMAT rtvFormat, DXGI_FORMAT uavFormat)
{
    m_texture = texture;
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = rtvFormat;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    HRESULT result = renderer->dxDevice()->CreateRenderTargetView(m_texture, &rtvDesc, &m_renderTargetView);
    HCHECK(result);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = uavFormat;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    result = renderer->dxDevice()->CreateUnorderedAccessView(m_texture, &uavDesc, &m_unorderedAccessView);
    HCHECK(result);
}

D3D11DepthRenderTexture::D3D11DepthRenderTexture(D3D11Renderer* renderer, int32_t width, int32_t height, ID3D11Texture2D* depthTexture, DXGI_FORMAT depthFormat)
    : RenderTexture(width, height)
    , m_depthTexture(nullptr)
{
    if (depthTexture) depthTexture->AddRef();
    createDepthStencilView(renderer, depthTexture, depthFormat);
}

D3D11DepthRenderTexture::~D3D11DepthRenderTexture()
{
    m_depthStencilView->Release();
    m_depthTexture->Release();
}

void D3D11DepthRenderTexture::createDepthStencilView(D3D11Renderer* renderer, ID3D11Texture2D* depthTexture, DXGI_FORMAT depthFormat)
{
    m_depthTexture = depthTexture;

    HRESULT result{};
    DXGI_FORMAT depth_format{};
    if (!m_depthTexture) {
        depth_format = DXGI_FORMAT_D24_UNORM_S8_UINT;

        CD3D11_TEXTURE2D_DESC depthStencilDesc(
            DXGI_FORMAT_D24_UNORM_S8_UINT, static_cast<UINT>(m_width), static_cast<UINT>(m_height), 1, 1, D3D11_BIND_DEPTH_STENCIL);

        result = renderer->dxDevice()->CreateTexture2D(&depthStencilDesc, nullptr, &m_depthTexture);
        if (result != S_OK) {
            printf("Failed to create DX texture: %d", result);
            abort();
        }
    } else {
        depth_format = depthFormat;
    }

    CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(D3D11_DSV_DIMENSION_TEXTURE2D, depth_format);
    result = renderer->dxDevice()->CreateDepthStencilView(m_depthTexture, &depthStencilViewDesc, &m_depthStencilView);
    if (result != S_OK) {
        printf("Failed to create depth stencil view: %d", result);
        abort();
    }
}

D3D11Renderer::D3D11Renderer(varjo_Session* session, const RendererSettings& renderer_settings)
    : IRenderer(session, renderer_settings)
{
#ifdef _DEBUG
    UINT flags = D3D11_CREATE_DEVICE_DEBUG;
#else
    UINT flags = 0;
#endif
    ComPtr<IDXGIAdapter> adapter = getAdapter(varjo_D3D11GetLuid(session));
    HRESULT result = D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, 0, flags, nullptr, 0, D3D11_SDK_VERSION,
        &m_device, nullptr, &m_deviceContext);
    if (result != S_OK) {
        printf("Failed to create device: %s", getLastErrorString());
        abort();
    }

    createShaders();
    createInstanceBuffer();
    createPerFrameBuffers();

    createDepthStencilStates();
    createRasterizerState();

    createGridBlendState();
    createOcclusionResources();

    if (renderer_settings.showMirrorWindow()) {
        createMirrorWindow();
    }

    m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

D3D11Renderer::~D3D11Renderer()
{
    freeRendererResources();

    for (auto& buffer : m_perFrameBuffers) {
        buffer.constantBuffer->Release();
    }

    m_instanceBuffer.buffer->Release();

#ifdef NVAPI_ENABLED
    if (m_vrsResourceView) {
        m_vrsResourceView->Release();
    }
#endif

    if (m_vrsVisualizeShader) m_vrsVisualizeShader->Release();
    if (m_vrsVisualizeConstantBuffer) m_vrsVisualizeConstantBuffer->Release();
    if (m_vrsUav) m_vrsUav->Release();
    if (m_vrsTexture) m_vrsTexture->Release();
    m_depthStencilState->Release();
    m_gridDepthStencilState->Release();

    // Release background grid blend state
    if (m_gridBlendState) {
        m_gridBlendState->Release();
    }

    m_rasterizerState->Release();
    m_deviceContext->Release();
    m_device->Release();

    if (m_occlusionDepthStencilState) {
        m_occlusionDepthStencilState->Release();
    }

    for (int i = 0; i < 2; i++) {
        if (m_occlusionMeshVertexes[i]) {
            m_occlusionMeshVertexes[i]->Release();
        }
    }
}

std::shared_ptr<Geometry> D3D11Renderer::createGeometry(uint32_t vertexCount, uint32_t indexCount)
{
    return std::make_shared<D3D11Geometry>(this, vertexCount, indexCount);
}

std::shared_ptr<RenderTexture> D3D11Renderer::createColorTexture(int32_t width, int32_t height, varjo_Texture colorTexture)
{
    if (m_settings.useVrs() && m_vrsTexture == nullptr) {
        enableVrs(width, height);
    }

    m_colorTextureSize = {width, height};

    auto nativeTexture = varjo_ToD3D11Texture(colorTexture);
    return std::make_shared<D3D11ColorRenderTexture>(
        this, width, height, nativeTexture, m_settings.noSrgb() ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM);
}

std::shared_ptr<RenderTexture> D3D11Renderer::createDepthTexture(int32_t width, int32_t height, varjo_Texture depthTexture)
{
    DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;
    auto nativeDepthTexture = varjo_ToD3D11Texture(depthTexture);

    if (nativeDepthTexture) {
        switch (m_depthSwapChainConfig.textureFormat) {
            case varjo_DepthTextureFormat_D32_FLOAT: {
                depthFormat = DXGI_FORMAT_D32_FLOAT;
                break;
            }
            case varjo_DepthTextureFormat_D24_UNORM_S8_UINT: {
                depthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
                break;
            }
            case varjo_DepthTextureFormat_D32_FLOAT_S8_UINT: {
                depthFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                break;
            }
            default:
                printf("ERROR: Unsupported depth stencil texture format: %d\n", static_cast<int>(m_depthSwapChainConfig.textureFormat));
                abort();
                break;
        }
    }

    return std::make_shared<D3D11DepthRenderTexture>(this, width, height, nativeDepthTexture, depthFormat);
}

std::shared_ptr<RenderTexture> D3D11Renderer::createVelocityTexture(int32_t width, int32_t height, varjo_Texture velocityTexture)
{
    auto nativeTexture = varjo_ToD3D11Texture(velocityTexture);
    return std::make_shared<D3D11ColorRenderTexture>(this, width, height, nativeTexture, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_UINT);
}

bool D3D11Renderer::isVrsSupported() const
{
#ifdef NVAPI_ENABLED
    NV_D3D1x_GRAPHICS_CAPS caps{};
    const NvAPI_Status NvStatus = NvAPI_D3D1x_GetGraphicsCapabilities(m_device, NV_D3D1x_GRAPHICS_CAPS_VER, &caps);
    return (NvStatus == NVAPI_OK && caps.bVariablePixelRateShadingSupported);
#else
    return false;
#endif
}

void D3D11Renderer::finishRendering() {}

bool D3D11Renderer::initVarjo()
{
    // Initialize Varjo graphics API to use D3D11.

    createSwapchains();

    // Check if the initialization was successful.
    const varjo_Error error = varjo_GetError(m_session);
    if (error != varjo_NoError) {
        printf(varjo_GetErrorDesc(error));
        return false;
    }

    return true;
}

void D3D11Renderer::createMirrorWindow()
{
    glm::ivec2 windowSize = getMirrorWindowSize();
    m_window = std::make_unique<Window>(windowSize.x, windowSize.y, false);

    Microsoft::WRL::ComPtr<IDXGIDevice2> dxgiDevice;
    HCHECK(m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)));

    Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
    HCHECK(dxgiDevice->GetParent(IID_PPV_ARGS(&dxgiAdapter)));

    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory;
    HCHECK(dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));

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

    HCHECK(dxgiFactory->CreateSwapChainForHwnd(m_device, m_window->getHandle(), &swapChainDesc, nullptr, nullptr, m_windowSwapChain.GetAddressOf()));
    HCHECK(m_windowSwapChain->GetBuffer(0, IID_PPV_ARGS(&m_windowBackBufferTexture)));
}


varjo_SwapChain* D3D11Renderer::createSwapChain(varjo_SwapChainConfig2& swapchainConfig)
{
    return varjo_D3D11CreateSwapChain(m_session, m_device, &swapchainConfig);
}

void D3D11Renderer::createSwapchains()
{
    // create color texture swap chain
    m_swapChainConfig.numberOfTextures = 3;
    m_swapChainConfig.textureArraySize = 1;
    m_swapChainConfig.textureFormat = m_settings.noSrgb() ? varjo_TextureFormat_R8G8B8A8_UNORM : varjo_TextureFormat_R8G8B8A8_SRGB;
    m_swapChainConfig.textureWidth = getTotalViewportsWidth();
    m_swapChainConfig.textureHeight = getTotalViewportsHeight();

    m_colorSwapChain = varjo_D3D11CreateSwapChain(m_session, m_device, &m_swapChainConfig);

    if (m_settings.useDepthLayers()) {
        // create depth texture swap chain
        m_depthSwapChainConfig = m_swapChainConfig;
        m_depthSwapChainConfig.textureFormat = m_settings.depthFormat();
        m_depthSwapChain = varjo_D3D11CreateSwapChain(m_session, m_device, &m_depthSwapChainConfig);
    }
    if (m_settings.useVelocity()) {
        // create velocity texture swap chain
        m_velocitySwapChainConfig = m_swapChainConfig;
        m_velocitySwapChainConfig.textureFormat = varjo_VelocityTextureFormat_R8G8B8A8_UINT;
        m_velocitySwapChain = varjo_D3D11CreateSwapChain(m_session, m_device, &m_velocitySwapChainConfig);
    }
}

void D3D11Renderer::bindRenderTarget(const RenderTargetTextures& renderTarget)
{
    auto dxRenderTarget = std::static_pointer_cast<D3D11ColorRenderTexture, RenderTexture>(renderTarget.getColorTexture());
    ID3D11RenderTargetView* colorRenderTargetView = dxRenderTarget ? dxRenderTarget->renderTargetView() : nullptr;

    auto dxDepthTarget = std::static_pointer_cast<D3D11DepthRenderTexture, RenderTexture>(renderTarget.getDepthTexture());
    ID3D11DepthStencilView* depthRenderTargetView = dxDepthTarget ? dxDepthTarget->depthStencilView() : nullptr;

    auto dxVelocityTarget = std::static_pointer_cast<D3D11ColorRenderTexture, RenderTexture>(renderTarget.getVelocityTexture());
    ID3D11RenderTargetView* velocityRenderTargetView = dxVelocityTarget ? dxVelocityTarget->renderTargetView() : nullptr;

    ID3D11RenderTargetView* tgts[2]{colorRenderTargetView, velocityRenderTargetView};
    const UINT targetCount = velocityRenderTargetView ? 2 : 1;
    m_deviceContext->OMSetRenderTargets(targetCount, tgts, depthRenderTargetView);

    m_currentColorTexture = dxRenderTarget;
}

void D3D11Renderer::unbindRenderTarget()
{
    ID3D11RenderTargetView* nullTgts[2]{nullptr, nullptr};
    m_deviceContext->OMSetRenderTargets(2, nullTgts, nullptr);
}

void D3D11Renderer::clearRenderTarget(const RenderTargetTextures& renderTarget, float r, float g, float b, float a)
{
    if (renderTarget.getColorTexture()) {
        auto dxRenderTarget = std::static_pointer_cast<D3D11ColorRenderTexture, RenderTexture>(renderTarget.getColorTexture());
        ID3D11RenderTargetView* colorRenderTargetView = dxRenderTarget ? dxRenderTarget->renderTargetView() : nullptr;
        const float color[]{r, g, b, a};
        m_deviceContext->ClearRenderTargetView(dxRenderTarget->renderTargetView(), color);
    }
    if (renderTarget.getDepthTexture()) {
        auto dxDepthTarget = std::static_pointer_cast<D3D11DepthRenderTexture, RenderTexture>(renderTarget.getDepthTexture());
        ID3D11DepthStencilView* depthStencilView = dxDepthTarget ? dxDepthTarget->depthStencilView() : nullptr;
        m_deviceContext->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, m_settings.useReverseDepth() ? 0.0f : 1.0f, 0);
    }
    if (renderTarget.getVelocityTexture()) {
        const float zeroVelocity[]{0.0f, 0.0f, 0.0f, 0.0f};
        auto dxVelocityTarget = std::static_pointer_cast<D3D11ColorRenderTexture, RenderTexture>(renderTarget.getVelocityTexture());
        ID3D11RenderTargetView* velocityRenderTargetView = dxVelocityTarget ? dxVelocityTarget->renderTargetView() : nullptr;
        m_deviceContext->ClearRenderTargetView(velocityRenderTargetView, zeroVelocity);
    }
}

void D3D11Renderer::freeCurrentRenderTarget() {}

void D3D11Renderer::useGeometry(const std::shared_ptr<Geometry>& geometry)
{
    auto dxGeometry = std::static_pointer_cast<D3D11Geometry, Geometry>(geometry);

    ID3D11Buffer* vertexBuffer = dxGeometry->vertexBuffer();

    uint32_t stride = sizeof(Geometry::Vertex);
    uint32_t offset = 0;

    m_deviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    m_deviceContext->IASetIndexBuffer(dxGeometry->indexBuffer(), DXGI_FORMAT_R32_UINT, 0);

    m_currentGeometry = geometry;
}

void D3D11Renderer::setupCamera(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
{
    m_shaderConstants.viewMatrix = glm::transpose(viewMatrix);
    m_shaderConstants.projectionMatrix = glm::transpose(projectionMatrix);

    // Update constants.
    m_deviceContext->UpdateSubresource(m_perFrameBuffers[m_currentFrameBuffer].constantBuffer, 0, nullptr, &m_shaderConstants, 0, 0);
    m_deviceContext->VSSetConstantBuffers(0, 1, &m_perFrameBuffers[m_currentFrameBuffer].constantBuffer);
}

void D3D11Renderer::setViewport(const varjo_Viewport& viewport)
{
    D3D11_VIEWPORT vp{};

    vp.TopLeftX = static_cast<float>(viewport.x);
    vp.TopLeftY = static_cast<float>(viewport.y);
    vp.Width = static_cast<float>(viewport.width);
    vp.Height = static_cast<float>(viewport.height);
    vp.MinDepth = 0;
    vp.MaxDepth = 1;

    m_deviceContext->RSSetViewports(1, &vp);

    m_shaderConstants.viewportSize = {vp.Width, vp.Height};
}

void D3D11Renderer::updateVrsMap(const varjo_Viewport& viewport)
{
#ifdef NVAPI_ENABLED
    const int32_t tileSize = NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH;
    varjo_VariableRateShadingConfig config = getDefaultVRSConfig(m_currentViewIndex, viewport, tileSize, m_settings, m_renderingGaze);
    varjo_D3D11UpdateVariableRateShadingTexture(m_session, m_device, m_vrsTexture, &config, &varjoShadingRateTable);
#endif
}

void D3D11Renderer::uploadInstanceBuffer(const std::vector<std::vector<ObjectRenderData>>& matrices)
{
    m_instanceBuffer.drawsOffsetCount.resize(0);
    m_instanceBuffer.drawsOffsetCount.reserve(matrices.size());
    std::size_t dataSize = 0;
    for (const auto& singleDrawMatrices : matrices) {
        dataSize += singleDrawMatrices.size();
    }
    std::vector<ObjectRenderData> instanceBufferData;
    instanceBufferData.reserve(dataSize);
    std::size_t dataOffset = 0;
    for (const auto& singleDrawMatrices : matrices) {
        m_instanceBuffer.drawsOffsetCount.push_back(std::make_pair(dataOffset, singleDrawMatrices.size()));
        dataOffset += singleDrawMatrices.size() * sizeof(ObjectRenderData);
        instanceBufferData.insert(instanceBufferData.end(), singleDrawMatrices.begin(), singleDrawMatrices.end());
    }

    // Map the instance buffer.
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = m_deviceContext->Map(m_instanceBuffer.buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (result != S_OK) {
        printf("Failed to map instance buffer: %s, %d", getLastErrorString(), GetLastError());
        exit(1);
    }

    m_instanceBuffer.data = reinterpret_cast<ObjectRenderData*>(mapped.pData);
    memcpy(m_instanceBuffer.data, instanceBufferData.data(), instanceBufferData.size() * sizeof(ObjectRenderData));

    m_deviceContext->Unmap(m_instanceBuffer.buffer, 0);
}

void D3D11Renderer::drawGrid()
{
    m_deviceContext->OMSetDepthStencilState(m_gridDepthStencilState, 0);

    // Set background grid blend state
    if (m_settings.useRenderVST()) {
        float blendFactors[] = {1, 1, 1, 1};
        m_deviceContext->OMSetBlendState(m_gridBlendState, blendFactors, 0xffffffff);
    }

    m_deviceContext->VSSetShader(m_gridShader.vertexShader, nullptr, 0);
    m_deviceContext->IASetInputLayout(m_gridShader.inputLayout);
    m_deviceContext->PSSetShader(m_gridShader.pixelShader, nullptr, 0);
    m_deviceContext->DrawIndexed(m_currentGeometry->indexCount(), 0, 0);

    m_deviceContext->OMSetDepthStencilState(m_depthStencilState, 0);

    // Reset background grid blend state
    if (m_settings.useRenderVST()) {
        m_deviceContext->OMSetBlendState(NULL, 0, 0xffffffff);
    }
}

void D3D11Renderer::drawObjects(std::size_t objectsIndex)
{
    const auto& drawOffsetCount = m_instanceBuffer.drawsOffsetCount[objectsIndex];

    UINT stride = sizeof(ObjectRenderData);
    UINT offset = static_cast<UINT>(drawOffsetCount.first);
    m_deviceContext->IASetVertexBuffers(1, 1, &m_instanceBuffer.buffer, &stride, &offset);

    m_deviceContext->VSSetShader(m_defaultShader.vertexShader, nullptr, 0);
    m_deviceContext->IASetInputLayout(m_defaultShader.inputLayout);
    m_deviceContext->PSSetShader(m_defaultShader.pixelShader, nullptr, 0);
    m_deviceContext->DrawIndexedInstanced(m_currentGeometry->indexCount(), static_cast<UINT>(drawOffsetCount.second), 0, 0, 0);
}

void D3D11Renderer::drawMirrorWindow()
{
    int32_t index;
    varjo_AcquireSwapChainImage(m_mirrorSwapchain, &index);
    if (varjo_GetError(m_session) == varjo_NoError) {
        const varjo_Texture swapchainTexture = varjo_GetSwapChainImage(m_mirrorSwapchain, index);
        ID3D11Texture2D* src = varjo_ToD3D11Texture(swapchainTexture);

        for (int i = 0; i < 2; i++) {
            const varjo_MirrorView& view = m_mirrorViews[i];
            D3D11_BOX copyBox;
            copyBox.front = 0;
            copyBox.back = 1;
            copyBox.left = view.viewport.x;
            copyBox.top = view.viewport.y;
            copyBox.right = view.viewport.x + view.viewport.width;
            copyBox.bottom = view.viewport.y + view.viewport.height;
            m_deviceContext->CopySubresourceRegion(m_windowBackBufferTexture.Get(), 0, view.viewport.x, view.viewport.y, 0, src, 0, &copyBox);
        }
        varjo_ReleaseSwapChainImage(m_mirrorSwapchain);
    }
}

void D3D11Renderer::advance() { m_currentFrameBuffer = (m_currentFrameBuffer + 1) % m_perFrameBuffers.size(); }

void D3D11Renderer::createShaders()
{
    createShader();
    createGridShader();
    if (m_settings.useVrs()) {
        createVrsVisualizeShader();
    }
}

void D3D11Renderer::createShader()
{
    ID3DBlob* compiledVertexShader = D3DShaders::compileDefaultVertexShader(m_settings);
    ID3DBlob* compiledPixelShader = D3DShaders::compileDefaultPixelShader(m_settings);

    HRESULT result =
        m_device->CreateVertexShader(compiledVertexShader->GetBufferPointer(), compiledVertexShader->GetBufferSize(), nullptr, &m_defaultShader.vertexShader);
    if (result != S_OK) {
        printf("Failed to compile vertex shader: %d", GetLastError());
        abort();
    }

    result = m_device->CreatePixelShader(compiledPixelShader->GetBufferPointer(), compiledPixelShader->GetBufferSize(), nullptr, &m_defaultShader.pixelShader);
    if (result != S_OK) {
        printf("Failed to compile pixel shader: %d", GetLastError());
        abort();
    }

    D3D11_INPUT_ELEMENT_DESC inputElements[10] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 7, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };

    result = m_device->CreateInputLayout(
        inputElements, _countof(inputElements), compiledVertexShader->GetBufferPointer(), compiledVertexShader->GetBufferSize(), &m_defaultShader.inputLayout);
    if (result != S_OK) {
        printf("Failed to create input layout: %d", GetLastError());
        abort();
    }
}

void D3D11Renderer::createGridShader()
{
    ID3DBlob* compiledVertexShader = D3DShaders::compileGridVertexShader();
    ID3DBlob* compiledPixelShader = D3DShaders::compileGridPixelShader();

    HRESULT result =
        m_device->CreateVertexShader(compiledVertexShader->GetBufferPointer(), compiledVertexShader->GetBufferSize(), nullptr, &m_gridShader.vertexShader);
    if (result != S_OK) {
        printf("Failed to compile vertex shader: %d", GetLastError());
        abort();
    }

    result = m_device->CreatePixelShader(compiledPixelShader->GetBufferPointer(), compiledPixelShader->GetBufferSize(), nullptr, &m_gridShader.pixelShader);
    if (result != S_OK) {
        printf("Failed to compile pixel shader: %d", GetLastError());
        abort();
    }

    D3D11_INPUT_ELEMENT_DESC inputElements[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    result = m_device->CreateInputLayout(
        inputElements, _countof(inputElements), compiledVertexShader->GetBufferPointer(), compiledVertexShader->GetBufferSize(), &m_gridShader.inputLayout);
    if (result != S_OK) {
        printf("Failed to create input layout: %d", GetLastError());
        abort();
    }
}

void D3D11Renderer::createVrsVisualizeShader()
{
    ID3DBlob* compiledVrsVisualizeShader = D3DShaders::compileVrsVisualizeShader();
    HRESULT result = m_device->CreateComputeShader(
        compiledVrsVisualizeShader->GetBufferPointer(), compiledVrsVisualizeShader->GetBufferSize(), nullptr, &m_vrsVisualizeShader);
    HCHECK(result);
}

void D3D11Renderer::createOcclusionShader()
{
    ID3DBlob* compiledVertexShader = D3DShaders::compileOcclusionVertexShader();
    ID3DBlob* compiledPixelShader = D3DShaders::compileOcclusionPixelShader();

    HRESULT result =
        m_device->CreateVertexShader(compiledVertexShader->GetBufferPointer(), compiledVertexShader->GetBufferSize(), nullptr, &m_occlusionShader.vertexShader);
    if (result != S_OK) {
        printf("Failed to compile vertex shader: %d", GetLastError());
        abort();
    }

    result =
        m_device->CreatePixelShader(compiledPixelShader->GetBufferPointer(), compiledPixelShader->GetBufferSize(), nullptr, &m_occlusionShader.pixelShader);
    if (result != S_OK) {
        printf("Failed to compile pixel shader: %d", GetLastError());
        abort();
    }

    D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    result = m_device->CreateInputLayout(inputElements, _countof(inputElements), compiledVertexShader->GetBufferPointer(),
        compiledVertexShader->GetBufferSize(), &m_occlusionShader.inputLayout);
    if (result != S_OK) {
        printf("Failed to create input layout: %d", GetLastError());
        abort();
    }
}

void D3D11Renderer::createDepthStencilStates()
{
    D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.StencilEnable = m_settings.useOcclusionMesh();
    depthStencilDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    depthStencilDesc.StencilWriteMask = 0;

    D3D11_DEPTH_STENCILOP_DESC stencilDesc;
    stencilDesc.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    stencilDesc.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    stencilDesc.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    stencilDesc.StencilFunc = D3D11_COMPARISON_EQUAL;

    depthStencilDesc.FrontFace = stencilDesc;
    depthStencilDesc.BackFace = stencilDesc;

    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = m_settings.useReverseDepth() ? D3D11_COMPARISON_GREATER : D3D11_COMPARISON_LESS;

    HRESULT result = m_device->CreateDepthStencilState(&depthStencilDesc, &m_depthStencilState);
    if (result != S_OK) {
        printf("Failed to create depth stencil state: %d", result);
        abort();
    }

    depthStencilDesc.DepthEnable = false;
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;

    result = m_device->CreateDepthStencilState(&depthStencilDesc, &m_gridDepthStencilState);
    if (result != S_OK) {
        printf("Failed to create grid depth stencil state: %d", result);
        abort();
    }
}

void D3D11Renderer::createRasterizerState()
{
    D3D11_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_BACK;
    rasterizerDesc.FrontCounterClockwise = true;
    rasterizerDesc.DepthBias = 0;
    rasterizerDesc.DepthBiasClamp = 0;
    rasterizerDesc.SlopeScaledDepthBias = 0.0f;
    rasterizerDesc.DepthClipEnable = true;
    rasterizerDesc.ScissorEnable = false;
    rasterizerDesc.MultisampleEnable = false;
    rasterizerDesc.AntialiasedLineEnable = false;

    HRESULT result = m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterizerState);
    if (result != S_OK) {
        printf("Failed to create rasterizer state: %d", GetLastError());
        abort();
    }

    m_deviceContext->RSSetState(m_rasterizerState);
}

void D3D11Renderer::createGridBlendState()
{
    // Create background grid blend state
    if (m_settings.useRenderVST()) {
        D3D11_BLEND_DESC blendDesc;
        memset(&blendDesc, 0, sizeof(blendDesc));
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        auto result = m_device->CreateBlendState(&blendDesc, &m_gridBlendState);
        if (result != S_OK) {
            printf("Failed to create blend state: %s", getLastErrorString());
            abort();
        }
    }
}

void D3D11Renderer::enableVrs(int32_t width, int32_t height)
{
    createVrsResources(width, height);
    setShadingRateAndResourceView();
}

void D3D11Renderer::createVrsResources(int32_t width, int32_t height)
{
#ifdef NVAPI_ENABLED
    m_vrsTextureSize.x = width / NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH;
    m_vrsTextureSize.y = height / NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT;

    D3D11_TEXTURE2D_DESC vrsDesc{};
    vrsDesc.Width = m_vrsTextureSize.x;
    vrsDesc.Height = m_vrsTextureSize.y;
    vrsDesc.ArraySize = 1;
    vrsDesc.MipLevels = 1;
    vrsDesc.Format = DXGI_FORMAT_R8_UINT;
    vrsDesc.SampleDesc.Count = 1;
    vrsDesc.SampleDesc.Quality = 0;
    vrsDesc.Usage = D3D11_USAGE_DEFAULT;
    vrsDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    vrsDesc.CPUAccessFlags = 0;
    vrsDesc.MiscFlags = 0;
    HRESULT result = m_device->CreateTexture2D(&vrsDesc, nullptr, &m_vrsTexture);
    HCHECK(result);

    NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC vrsResourceViewDesc{};
    vrsResourceViewDesc.version = NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER;
    vrsResourceViewDesc.ViewDimension = NV_SRRV_DIMENSION_TEXTURE2D;
    vrsResourceViewDesc.Texture2D.MipSlice = 0;
    vrsResourceViewDesc.Format = DXGI_FORMAT_R8_UINT;
    const NvAPI_Status nvStatus = NvAPI_D3D11_CreateShadingRateResourceView(m_device, m_vrsTexture, &vrsResourceViewDesc, &m_vrsResourceView);
    if (nvStatus != NVAPI_OK) {
        printf("Failed to create shading rate resource view: %d\n", nvStatus);
        abort();
    }

    if (m_settings.visualizeVrs()) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R8_UINT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        result = m_device->CreateUnorderedAccessView(m_vrsTexture, &uavDesc, &m_vrsUav);
        HCHECK(result);

        D3D11_BUFFER_DESC bufferDesc{};
        bufferDesc.ByteWidth = sizeof(float) * 4;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        result = m_device->CreateBuffer(&bufferDesc, nullptr, &m_vrsVisualizeConstantBuffer);
        HCHECK(result);
    }
#endif
}

void D3D11Renderer::setShadingRateAndResourceView()
{
#ifdef NVAPI_ENABLED
    m_viewportShadingRateDesc.enableVariablePixelShadingRate = true;
    for (uint32_t i = 0; i < c_shadingRateCount; ++i) {
        m_viewportShadingRateDesc.shadingRateTable[i] = c_nvShadingRates[i];
    }

    m_viewportsShadingRateDesc.version = NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;
    m_viewportsShadingRateDesc.numViewports = 1;
    m_viewportsShadingRateDesc.pViewports = &m_viewportShadingRateDesc;

    NvAPI_Status nvStatus = NvAPI_D3D11_RSSetViewportsPixelShadingRates(m_deviceContext, &m_viewportsShadingRateDesc);
    if (nvStatus != NVAPI_OK) {
        printf("Failed to set viewports pixel shadering rates: %d\n", nvStatus);
        abort();
    }

    nvStatus = NvAPI_D3D11_RSSetShadingRateResourceView(m_deviceContext, m_vrsResourceView);
    if (nvStatus != NVAPI_OK) {
        printf("Failed to set shading rate resource view: %d\n", nvStatus);
        abort();
    }
#endif
}

varjo_ClipRange D3D11Renderer::getClipRange() const { return varjo_ClipRangeZeroToOne; }

void D3D11Renderer::createPerFrameBuffers()
{
    for (int i = 0; i < 4 * 4; ++i) {
        auto constantBuffer = createConstantBuffer();

        m_perFrameBuffers.push_back({constantBuffer});
    }
}

void D3D11Renderer::createInstanceBuffer()
{
    const int32_t maxInstances = 5000;
    m_instanceBuffer.maxInstances = maxInstances;

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.ByteWidth = sizeof(glm::mat4) * maxInstances;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.StructureByteStride = 0;

    HRESULT result = m_device->CreateBuffer(&bufferDesc, nullptr, &m_instanceBuffer.buffer);
    if (result != S_OK) {
        printf("Failed to create constant buffer: %d", GetLastError());
        abort();
    }
}

void D3D11Renderer::renderOcclusionMesh()
{
    if (m_settings.useOcclusionMesh() && m_currentViewIndex < 2) {
        renderOcclusionMesh(m_currentViewIndex);
    }
}

void D3D11Renderer::renderOcclusionMesh(int32_t viewIndex) const
{
    m_deviceContext->OMSetDepthStencilState(m_occlusionDepthStencilState, 0);
    UINT stride = sizeof(float) * 2;
    UINT offset = 0;
    m_deviceContext->IASetVertexBuffers(0, 1, &m_occlusionMeshVertexes[viewIndex], &stride, &offset);
    m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_deviceContext->IASetInputLayout(m_occlusionShader.inputLayout);
    m_deviceContext->VSSetShader(m_occlusionShader.vertexShader, nullptr, 0);
    m_deviceContext->PSSetShader(m_occlusionShader.pixelShader, nullptr, 0);

    m_deviceContext->Draw(m_occlusionMeshVertexCount[viewIndex], 0);
}

void D3D11Renderer::recreateOcclusionMesh(uint32_t viewIndex)
{
    if (m_settings.useOcclusionMesh() && viewIndex < 2) {
        if (m_occlusionMeshVertexes[viewIndex]) {
            m_occlusionMeshVertexes[viewIndex]->Release();
            m_occlusionMeshVertexes[viewIndex] = nullptr;
        }
        createOcclusionMeshBuffer(viewIndex);
    }
}

void D3D11Renderer::postRenderFrame()
{
    if (m_window) {
        m_window->present(m_windowSwapChain.Get());
    }

    if (!m_settings.visualizeVrs() || !m_currentColorTexture) {
        return;
    }

    ID3D11UnorderedAccessView* colorUav = m_currentColorTexture->unorderedAccessView();
    if (!colorUav) {
        return;
    }

    ID3D11RenderTargetView* nullRtvs[2]{NULL, NULL};
    m_deviceContext->OMSetRenderTargets(2, nullRtvs, NULL);

    std::vector<glm::vec2> bufferData{m_colorTextureSize, m_vrsTextureSize};
    m_deviceContext->UpdateSubresource(m_vrsVisualizeConstantBuffer, 0, nullptr, bufferData.data(), 0, 0);
    m_deviceContext->CSSetConstantBuffers(0, 1, &m_vrsVisualizeConstantBuffer);

    m_deviceContext->CSSetShader(m_vrsVisualizeShader, nullptr, 0);
    m_deviceContext->CSSetUnorderedAccessViews(0, 1, &colorUav, nullptr);
    m_deviceContext->CSSetUnorderedAccessViews(1, 1, &m_vrsUav, nullptr);
    m_deviceContext->Dispatch(m_colorTextureSize.x / 8, m_colorTextureSize.y / 8, 1);

    ID3D11UnorderedAccessView* nullUav[] = {NULL};
    m_deviceContext->CSSetUnorderedAccessViews(1, 1, nullUav, nullptr);
}

ID3D11Buffer* D3D11Renderer::createConstantBuffer()
{
    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.ByteWidth = sizeof m_shaderConstants;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.StructureByteStride = 0;

    ID3D11Buffer* buffer = nullptr;
    HRESULT result = m_device->CreateBuffer(&bufferDesc, nullptr, &buffer);
    if (result != S_OK) {
        printf("Failed to create constant buffer: %d", GetLastError());
        abort();
    }
    return buffer;
}

void D3D11Renderer::createOcclusionResources()
{
    if (m_settings.useOcclusionMesh()) {
        createOcclusionDepthStencilState();
        for (uint32_t viewIndex = 0; viewIndex < 2; viewIndex++) {
            createOcclusionMeshBuffer(viewIndex);
        }
        createOcclusionShader();
    }
}

void D3D11Renderer::createOcclusionMeshBuffer(uint32_t viewIndex)
{
    varjo_Mesh2Df* mesh = varjo_CreateOcclusionMesh(m_session, viewIndex, varjo_WindingOrder_CounterClockwise);
    if (mesh->vertexCount == 0) {
        return;
    }

    m_occlusionMeshVertexCount[viewIndex] = mesh->vertexCount;

    D3D11_BUFFER_DESC vDesc{};
    const uint32_t dataSize = mesh->vertexCount * sizeof(varjo_Vector2Df);
    vDesc.ByteWidth = dataSize;
    vDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vDesc.Usage = D3D11_USAGE_DEFAULT;
    vDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA subresourceData{};
    subresourceData.pSysMem = mesh->vertices;
    subresourceData.SysMemPitch = 0;
    subresourceData.SysMemSlicePitch = 0;

    HCHECK(m_device->CreateBuffer(&vDesc, &subresourceData, &m_occlusionMeshVertexes[viewIndex]));
    varjo_FreeOcclusionMesh(mesh);
}

void D3D11Renderer::createOcclusionDepthStencilState()
{
    D3D11_DEPTH_STENCIL_DESC dsDesc{};

    // Disable depth test, we are writing 1 to stencil area with occlusion mask
    dsDesc.DepthEnable = false;

    // Stencil test parameters
    dsDesc.StencilEnable = true;
    // Putting stencil buffer into write only mode
    dsDesc.StencilReadMask = 0;
    dsDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;

    D3D11_DEPTH_STENCILOP_DESC stencilDesc;
    stencilDesc.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    stencilDesc.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    // Increase stencil value by 1 (basically write 1 to stencil buffer where occlusion mask is)
    stencilDesc.StencilPassOp = D3D11_STENCIL_OP_INCR;
    stencilDesc.StencilFunc = D3D11_COMPARISON_ALWAYS;

    dsDesc.FrontFace = stencilDesc;
    dsDesc.BackFace = stencilDesc;

    // Create depth stencil state
    HCHECK(m_device->CreateDepthStencilState(&dsDesc, &m_occlusionDepthStencilState));
}

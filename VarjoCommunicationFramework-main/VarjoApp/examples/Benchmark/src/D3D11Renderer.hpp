#pragma once

#include <vector>
#include <array>
#include <DirectXMath.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <windows.h>
#include <wrl.h>
#include <Varjo_d3d11.h>
#ifdef NVAPI_ENABLED
// NVApi is a library provided by NVidia to enable variable rate shading
// For more info, see:
// NVidia documentation https://developer.nvidia.com/vrworks
// Varjo documentation https://developer.varjo.com/
// The library is not redistributed by Varjo
#include <nvapi.h>
#endif

#include "IRenderer.hpp"
#include "Window.hpp"

class D3D11ColorRenderTexture final : public RenderTexture
{
public:
    D3D11ColorRenderTexture(D3D11Renderer* renderer, int32_t width, int32_t height, ID3D11Texture2D* texture, DXGI_FORMAT rtvFormat, DXGI_FORMAT uavFormat);
    ~D3D11ColorRenderTexture() override;

    varjo_Texture texture() const override { return varjo_FromD3D11Texture(m_texture); }
    ID3D11RenderTargetView* renderTargetView() const { return m_renderTargetView; }
    ID3D11UnorderedAccessView* unorderedAccessView() const { return m_unorderedAccessView; }

private:
    void createRtvAndUav(D3D11Renderer* renderer, ID3D11Texture2D* texture, DXGI_FORMAT rtvFormat, DXGI_FORMAT uavFormat);

    ID3D11RenderTargetView* m_renderTargetView{nullptr};
    ID3D11UnorderedAccessView* m_unorderedAccessView{nullptr};
    ID3D11Texture2D* m_texture{nullptr};
};

class D3D11DepthRenderTexture final : public RenderTexture
{
public:
    D3D11DepthRenderTexture(D3D11Renderer* renderer, int32_t width, int32_t height, ID3D11Texture2D* depthTexture, DXGI_FORMAT depthFormat);
    ~D3D11DepthRenderTexture() override;

    varjo_Texture texture() const override { return varjo_FromD3D11Texture(m_depthTexture); }
    ID3D11DepthStencilView* depthStencilView() const { return m_depthStencilView; }

private:
    void createDepthStencilView(D3D11Renderer* renderer, ID3D11Texture2D* depthTexture, DXGI_FORMAT depthFormat);

    ID3D11DepthStencilView* m_depthStencilView;
    ID3D11Texture2D* m_depthTexture{nullptr};
};

class D3D11Renderer final : public IRenderer
{
public:
    D3D11Renderer(varjo_Session* session, const RendererSettings& renderer_settings);
    ~D3D11Renderer() override;

    ID3D11Device* dxDevice() const { return m_device; }
    ID3D11DeviceContext* dxDeviceContext() const { return m_deviceContext; }

    std::shared_ptr<Geometry> createGeometry(uint32_t vertexCount, uint32_t indexCount) override;
    std::shared_ptr<RenderTexture> createColorTexture(int32_t width, int32_t height, varjo_Texture colorTexture) override;
    std::shared_ptr<RenderTexture> createDepthTexture(int32_t width, int32_t height, varjo_Texture depthTexture) override;
    std::shared_ptr<RenderTexture> createVelocityTexture(int32_t width, int32_t height, varjo_Texture velocityTexture) override;

    bool isVrsSupported() const override;
    void finishRendering() override;
    void recreateOcclusionMesh(uint32_t viewIndex) override;

protected:
    void renderOcclusionMesh() override;

private:
    bool initVarjo() override;
    void createSwapchains() override;
    varjo_SwapChain* createSwapChain(varjo_SwapChainConfig2& swapchainConfig) override;
    void createMirrorWindow();

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
    void advance() override;

    void createShaders();
    void createShader();
    void createGridShader();
    void createVrsVisualizeShader();

    void createDepthStencilStates();
    void createRasterizerState();
    void createGridBlendState();

    void enableVrs(int32_t width, int32_t height);
    void createVrsResources(int32_t width, int32_t height);
    void setShadingRateAndResourceView();

    varjo_ClipRange getClipRange() const override;

    void createPerFrameBuffers();
    void createInstanceBuffer();
    ID3D11Buffer* createConstantBuffer();
    void createOcclusionMeshBuffer(uint32_t viewIndex);
    void createOcclusionResources();
    void createOcclusionShader();
    void createOcclusionDepthStencilState();
    void postRenderFrame() override;
    void renderOcclusionMesh(int32_t viewIndex) const;

    struct ShaderConstants {
        glm::mat4 viewMatrix;
        glm::mat4 projectionMatrix;
        glm::vec2 viewportSize;
        glm::vec2 padding;
    };

    struct InstanceBuffer {
        ID3D11Buffer* buffer;
        int32_t maxInstances;
        std::vector<std::pair<std::size_t, std::size_t>> drawsOffsetCount;
        ObjectRenderData* data;
    };

    struct PerFrameBuffers {
        ID3D11Buffer* constantBuffer;
    };

    struct Shader {
        ~Shader()
        {
            if (vertexShader) vertexShader->Release();
            if (pixelShader) pixelShader->Release();
            if (inputLayout) inputLayout->Release();
        }

        ID3D11VertexShader* vertexShader = nullptr;
        ID3D11PixelShader* pixelShader = nullptr;
        ID3D11InputLayout* inputLayout = nullptr;
    };

    ShaderConstants m_shaderConstants;
    InstanceBuffer m_instanceBuffer = {};

    Shader m_defaultShader = {};
    Shader m_gridShader = {};
    Shader m_occlusionShader = {};

    int32_t m_currentFrameBuffer = 0;
    std::vector<PerFrameBuffers> m_perFrameBuffers;

    ID3D11DepthStencilState* m_depthStencilState = nullptr;
    ID3D11DepthStencilState* m_gridDepthStencilState = nullptr;

    ID3D11BlendState* m_gridBlendState = nullptr;

    ID3D11RasterizerState* m_rasterizerState = nullptr;
    ID3D11DeviceContext* m_deviceContext = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DepthStencilState* m_occlusionDepthStencilState = nullptr;

    std::array<ID3D11Buffer*, 2> m_occlusionMeshVertexes = {};
    std::array<int32_t, 2> m_occlusionMeshVertexCount = {};

    ID3D11Texture2D* m_vrsTexture = nullptr;
    ID3D11ComputeShader* m_vrsVisualizeShader = nullptr;
    ID3D11UnorderedAccessView* m_vrsUav = nullptr;
    ID3D11Buffer* m_vrsVisualizeConstantBuffer = nullptr;

    glm::ivec2 m_colorTextureSize;
    glm::ivec2 m_vrsTextureSize;

#ifdef NVAPI_ENABLED
    ID3D11NvShadingRateResourceView* m_vrsResourceView = nullptr;
    NV_D3D11_VIEWPORT_SHADING_RATE_DESC m_viewportShadingRateDesc{};
    NV_D3D11_VIEWPORTS_SHADING_RATE_DESC m_viewportsShadingRateDesc{};
#endif

    std::shared_ptr<D3D11ColorRenderTexture> m_currentColorTexture;


    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_windowSwapChain;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_windowBackBufferTexture;
};

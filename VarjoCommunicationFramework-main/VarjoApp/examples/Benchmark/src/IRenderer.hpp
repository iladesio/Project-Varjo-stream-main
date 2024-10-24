#pragma once

#include <vector>
#include <memory>
#include <Varjo.h>
#include <Varjo_types_layers.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <optional>

#include "Geometry.hpp"
#include "Window.hpp"

class RendererSettings final
{
public:
    RendererSettings() = default;
    RendererSettings(bool useDepthLayers, bool renderVST, bool depthTestVST, bool stereo, bool useOcclusionMesh, varjo_TextureFormat depthFormat,
        bool reverseDepth, bool useSli, bool useSlaveGpu, bool useDynamicViewports, bool useVrs, bool useGaze, bool visualizeVrs, bool useVelocity, bool noSrgb,
        bool showMirrorWindow)
        : m_useDepthLayers(useDepthLayers)
        , m_renderVST(renderVST)
        , m_depthTestVST(depthTestVST)
        , m_stereo(stereo)
        , m_useOcclusionMesh(useOcclusionMesh)
        , m_depthFormat(depthFormat)
        , m_useReverseDepth(reverseDepth)
        , m_useSli(useSli)
        , m_useSlaveGpu(useSlaveGpu)
        , m_useDynamicViewports(useDynamicViewports)
        , m_useVrs(useVrs)
        , m_useGaze(useGaze)
        , m_visualizeVrs(visualizeVrs)
        , m_useVelocity(useVelocity)
        , m_noSrgb(noSrgb)
        , m_showMirrorWindow(showMirrorWindow)
    {
    }

    bool useDepthLayers() const { return m_useDepthLayers; }
    bool useRenderVST() const { return m_renderVST; }
    bool useDepthTestVST() const { return m_depthTestVST; }
    bool useStereo() const { return m_stereo; }
    bool useOcclusionMesh() const { return m_useOcclusionMesh; }
    varjo_TextureFormat depthFormat() const { return m_depthFormat; }
    bool useReverseDepth() const { return m_useReverseDepth; }
    bool useSli() const { return m_useSli; }
    bool useSlaveGpu() const { return m_useSlaveGpu; }
    bool useDynamicViewports() const { return m_useDynamicViewports; }
    bool useVrs() const { return m_useVrs; }
    bool useGaze() const { return m_useGaze; }
    bool visualizeVrs() const { return m_visualizeVrs; }
    bool useVelocity() const { return m_useVelocity; }
    bool noSrgb() const { return m_noSrgb; }
    bool showMirrorWindow() const { return m_showMirrorWindow; }

    void setUseVrs(bool enabled) { m_useVrs = enabled; }
    void setVisualizeVrs(bool enabled) { m_visualizeVrs = enabled; }

private:
    bool m_useDepthLayers{false};
    bool m_renderVST{false};
    bool m_depthTestVST{false};
    bool m_stereo{false};
    bool m_useOcclusionMesh{false};
    varjo_TextureFormat m_depthFormat{varjo_DepthTextureFormat_D32_FLOAT};
    bool m_useReverseDepth{false};
    bool m_useSli{false};
    bool m_useSlaveGpu{false};
    bool m_useDynamicViewports{false};
    bool m_useVrs{false};
    bool m_useGaze{false};
    bool m_visualizeVrs{false};
    bool m_useVelocity{false};
    bool m_noSrgb{false};
    bool m_showMirrorWindow{false};
};

class RenderTexture
{
public:
    RenderTexture(int32_t width, int32_t height)
        : m_width(width)
        , m_height(height){};
    virtual ~RenderTexture() = default;

    int32_t width() const { return m_width; }
    int32_t height() const { return m_height; }

    virtual varjo_Texture texture() const = 0;

protected:
    int32_t m_width;
    int32_t m_height;
};

class RenderTargetTextures
{
public:
    RenderTargetTextures()
        : m_colorTexture(nullptr)
        , m_depthTexture(nullptr)
        , m_velocityTexture(nullptr)
    {
    }

    RenderTargetTextures(
        std::shared_ptr<RenderTexture>& colorTexture, std::shared_ptr<RenderTexture>& depthTexture, std::shared_ptr<RenderTexture>& velocityTexture)
        : m_colorTexture(colorTexture)
        , m_depthTexture(depthTexture)
        , m_velocityTexture(velocityTexture)
    {
    }

    void reset();

    const std::shared_ptr<RenderTexture> getColorTexture() const { return m_colorTexture; }
    const std::shared_ptr<RenderTexture> getDepthTexture() const { return m_depthTexture; }
    const std::shared_ptr<RenderTexture> getVelocityTexture() const { return m_velocityTexture; }

private:
    std::shared_ptr<RenderTexture> m_colorTexture;
    std::shared_ptr<RenderTexture> m_depthTexture;
    std::shared_ptr<RenderTexture> m_velocityTexture;
};

class IRenderer : public std::enable_shared_from_this<IRenderer>
{
public:
    static constexpr float c_velocityPrecision = 32.0f;         // Must match the constant used in shaders
    static constexpr float c_velocityTimeDelta = 1.0f / 60.0f;  // Can be any number. Smaller number works better for fast moving objects and vice versa.

    struct ObjectVelocity {
        glm::vec3 rotationAxis;
        float rotationSpeed;
    };
    struct Object {
        std::shared_ptr<Geometry> geometry;
        glm::vec3 position;
        glm::vec3 scale;
        glm::quat orientation;
        ObjectVelocity velocity;
    };
    struct ObjectRenderData {
        glm::mat4 world;
        glm::mat4 nextFrameWorld;  // Estimated world matrix at the next frame. Used to calculate velocity.
    };

    static void applyObjectVelocity(Object& object, float timeDeltaSec);

    IRenderer(varjo_Session* session, const RendererSettings& renderer_settings);
    virtual ~IRenderer() = default;

    bool init();

    void updateViewportLayout();

    virtual std::shared_ptr<Geometry> createGeometry(uint32_t vertexCount, uint32_t indexCount) = 0;

    virtual std::shared_ptr<RenderTexture> createColorTexture(int32_t width, int32_t height, varjo_Texture colorTexture) = 0;
    virtual std::shared_ptr<RenderTexture> createDepthTexture(int32_t width, int32_t height, varjo_Texture depthTexture) = 0;
    virtual std::shared_ptr<RenderTexture> createVelocityTexture(int32_t width, int32_t height, varjo_Texture velocityTexture) = 0;

    void render(varjo_FrameInfo* frameInfo, const std::vector<std::vector<Object>*>& instancedObjects);
    void useFoveatedViewports(bool use);
    virtual bool isVrsSupported() const = 0;
    void recreateSwapchains();
    virtual void recreateOcclusionMesh(uint32_t viewIndex) = 0;
    virtual void finishRendering() = 0;
    void freeVarjoResources();

    Window* getWindow() const { return m_window.get(); }

protected:
    // Initialize the Varjo graphics API.
    virtual bool initVarjo() = 0;
    virtual void createSwapchains() = 0;
    virtual varjo_SwapChain* createSwapChain(varjo_SwapChainConfig2& swapchainConfig) = 0;
    void freeSwapchainsAndRenderTargets();
    void freeRendererResources();
    void initViewports();
    void createRenderTargets();
    uint32_t getTotalViewportsWidth() const;
    uint32_t getTotalViewportsHeight() const;

    virtual void bindRenderTarget(const RenderTargetTextures& renderTarget) = 0;
    virtual void unbindRenderTarget() = 0;
    virtual void clearRenderTarget(const RenderTargetTextures& renderTarget, float r, float g, float b, float a) = 0;
    virtual void freeCurrentRenderTarget() = 0;

    virtual void useGeometry(const std::shared_ptr<Geometry>& geometry) = 0;

    virtual void setupCamera(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) = 0;
    virtual void setViewport(const varjo_Viewport& viewport) = 0;
    virtual void updateVrsMap(const varjo_Viewport& viewport) = 0;

    virtual void uploadInstanceBuffer(const std::vector<std::vector<ObjectRenderData>>& matrices) = 0;

    // Called before rendering views started
    virtual void preRenderFrame() {}
    // Called after frame is rendered but before submitted
    virtual void postRenderFrame() {}

    // Called before view rendering is started
    virtual void preRenderView() {}
    // Called after view rendering is finished
    virtual void postRenderView() {}

    virtual void renderOcclusionMesh() {}

    // Draw the background grid.
    virtual void drawGrid() = 0;

    // Queue objects to be drawn. Uses the current geometry.
    virtual void drawObjects(std::size_t objectsIndex) = 0;

    // Draw mirror window
    virtual void drawMirrorWindow() = 0;

    virtual void advance() = 0;

    virtual varjo_ClipRange getClipRange() const = 0;

    void drawObjects(const std::vector<Object>& objects, std::size_t drawIndex);

    static const char* getLastErrorString();

    std::vector<varjo_Viewport> calculateViewports(varjo_TextureSize_Type type) const;
    const varjo_Viewport& getActiveViewport(int32_t viewIndex) const;

    glm::ivec2 getMirrorWindowSize();

private:
    void calculateWorldMatrices(std::vector<IRenderer::ObjectRenderData>& worldMatrices, const std::vector<IRenderer::Object>& objects);

    const std::vector<varjo_Viewport>& getActiveViewports() const;

protected:
    static constexpr double c_nearClipDistance = .1;
    static constexpr double c_farClipDistance = 1000.;
    varjo_Session* m_session = nullptr;
    uint32_t m_viewCount{};
    uint32_t m_currentViewIndex{0};
    varjo_SwapChainConfig2 m_swapChainConfig{};
    varjo_SwapChainConfig2 m_depthSwapChainConfig{};
    varjo_SwapChainConfig2 m_velocitySwapChainConfig{};
    varjo_SwapChain* m_colorSwapChain = nullptr;
    varjo_SwapChain* m_depthSwapChain = nullptr;
    varjo_SwapChain* m_velocitySwapChain = nullptr;
    std::vector<varjo_LayerMultiProjView> m_multiprojectionViews;
    std::vector<varjo_ViewExtensionDepth> m_extDepthViews;
    std::vector<varjo_ViewExtensionVelocity> m_extVelocityViews;

    // Temp vector for object world matrices.
    std::vector<std::vector<ObjectRenderData>> m_objectWorldMatrices;

    // Currently active geometry.
    std::shared_ptr<Geometry> m_currentGeometry;

    std::shared_ptr<Geometry> m_cubeGeometry;

    std::vector<std::shared_ptr<RenderTexture>> m_colorTargets;
    std::vector<std::shared_ptr<RenderTexture>> m_depthTargets;
    std::vector<std::shared_ptr<RenderTexture>> m_velocityTargets;

    RendererSettings m_settings;
    std::optional<varjo_Gaze> m_renderingGaze;

    varjo_SwapChain* m_mirrorSwapchain;
    std::vector<varjo_MirrorView> m_mirrorViews{};

    std::unique_ptr<Window> m_window;

private:
    bool m_useFoveatedViewports{false};
    std::vector<varjo_Viewport> m_viewports;
    std::vector<varjo_Viewport> m_foveatedViewports;
};

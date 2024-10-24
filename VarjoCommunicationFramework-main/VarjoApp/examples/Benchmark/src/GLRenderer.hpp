#pragma once

#include <vector>
#include <array>
#include <Windows.h>
#include <GL/glew.h>
#include <Varjo_gl.h>

#include "IRenderer.hpp"
#include "Window.hpp"


class GLColorRenderTexture final : public RenderTexture
{
public:
    GLColorRenderTexture(int32_t width, int32_t height, GLuint texture);
    GLColorRenderTexture(int32_t width, int32_t height, varjo_TextureFormat format);
    ~GLColorRenderTexture() override;

    varjo_Texture texture() const override { return varjo_FromGLTexture(m_colorTexture.textureId); }

    GLuint backBuffer() const { return m_colorTexture.textureId; }

private:
    struct GLTextureRef {
        GLuint textureId;
        bool owned;
    };

    GLTextureRef m_colorTexture{};
};

class GLDepthRenderTexture final : public RenderTexture
{
public:
    GLDepthRenderTexture(int32_t width, int32_t height, GLuint texture, bool hasStencil);
    ~GLDepthRenderTexture() override;

    varjo_Texture texture() const override { return varjo_FromGLTexture(m_depthTexture.textureId); }

    GLuint depthBuffer() const { return m_depthTexture.textureId; }
    bool hasStencil() const { return m_hasStencil; }

    bool isRenderBuffer() const { return m_depthTexture.owned; }

private:
    struct GLTextureRef {
        GLuint textureId;
        bool owned;
    };

    GLTextureRef m_depthTexture{};
    bool m_hasStencil;
};


class GLRenderer final : public IRenderer
{
public:
    GLRenderer(varjo_Session* session, const RendererSettings& renderer_settings);
    ~GLRenderer() override;

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

    void bindRenderTarget(const RenderTargetTextures& renderTarget) override;
    void unbindRenderTarget() override;
    void clearRenderTarget(const RenderTargetTextures& renderTarget, float r, float g, float b, float a) override;
    void freeCurrentRenderTarget() override;

    void useGeometry(const std::shared_ptr<Geometry>& geometry) override;

    void setupCamera(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) override;
    void setViewport(const varjo_Viewport& viewport) override;
    void updateVrsMap(const varjo_Viewport& viewport) override;
    void uploadInstanceBuffer(const std::vector<std::vector<ObjectRenderData>>& matrices) override;

    void preRenderView() override;
    void postRenderView() override;
    void postRenderFrame() override;

    void drawGrid() override;
    void drawObjects(std::size_t objectsIndex) override;
    void drawMirrorWindow() override;
    void advance() override;

    void compileShaders();
    void compileShader();
    void compileGridShader();
    void compileStencilShader();
    void compileVrsVisualizeShader();

    void createPerFrameBuffers();
    void createInstanceBuffer();
    GLuint createUniformBuffer();
    void useInstanceBuffer(std::size_t offset);

    void createVrsResources(int32_t width, int32_t height);
    void loadVrsExtension();
    void createVrsTextures(int32_t width, int32_t height);
    void createVrsPalette();

    varjo_ClipRange getClipRange() const override;

    void createOcclusionMesh(uint32_t viewIndex);
    void renderOcclusionMesh(uint32_t viewIndex);

    GLbitfield getGpuMaskForView(uint32_t viewIndex);

    struct ShaderUniforms {
        glm::mat4 viewMatrix;
        glm::mat4 projectionMatrix;
        glm::vec2 viewportSize;
        glm::vec2 padding;
    };

    struct InstanceBuffer {
        GLuint buffer;
        int32_t maxInstances;
        std::vector<std::pair<std::size_t, std::size_t>> drawsOffsetCount;

        ObjectRenderData* data;
    };

    struct PerFrameBuffers {
        GLuint uniformBuffer;
    };

    struct CopyInformation {
        // all copies from GPU1 to GPU0
        GLint srcX;
        GLint srcY;
        GLint dstX;
        GLint dstY;
        GLint width;
        GLint height;
        GLuint srcBuffer;
        GLuint dstBuffer;
        ~CopyInformation()
        {
            glMulticastCopyImageSubDataNV(
                1, GPUMASK_0, srcBuffer, GL_TEXTURE_2D, 0, srcX, srcY, 0, dstBuffer, GL_TEXTURE_2D, 0, dstX, dstY, 0, width, height, 1);
        }
    };

    int32_t m_currentFrameBuffer = 0;
    std::vector<PerFrameBuffers> m_perFrameBuffers;

    InstanceBuffer m_instanceBuffer = {};
    ShaderUniforms m_shaderUniforms;

    HWND m_hwnd = 0;
    HDC m_hdc = 0;
    HGLRC m_hglrc = 0;

    GLuint m_frameBuffer = 0;

    GLuint m_gridProgram = 0;
    GLuint m_program = 0;
    GLuint m_occlusionMeshProgram = 0;
    GLuint m_visualizeVrsProgram = 0;
    // Focus screen doesn't have occlusion mesh and there are always at least 2 meshes
    std::array<GLuint, 2> m_occlusionMeshBuffers{};
    std::array<int32_t, 2> m_occlusionMeshVertexes{};

    static const GLbitfield GPUMASK_0 = 1 << 0;
    static const GLbitfield GPUMASK_1 = 1 << 1;
    static const GLbitfield GPUMASK_ALL = GPUMASK_0 | GPUMASK_1;

    bool m_multicast{false};
    varjo_Viewport m_currentViewPort{};
    RenderTargetTextures m_currentRenderTarget;
    std::vector<CopyInformation> m_pendingCopies;

    GLuint m_currentBackbuffer = 0;
    GLuint m_vrsTexture = 0;
    GLuint m_vrsVisualizationTexture = 0;
    int32_t m_vrsTileSize = 0;
    std::vector<varjo_ShadingRate> m_palette;
    glm::ivec2 m_colorTextureSize;
    glm::vec2 m_vrsTextureSize;
};

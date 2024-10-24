#include <GL/glew.h>
#include <GL/wglew.h>
#include <glm/gtc/type_ptr.hpp>

#include <string>

#include "GLRenderer.hpp"
#include "VRSHelper.hpp"
#include "Varjo_layers.h"

#include "Varjo_d3d11.h"
#include "wrl.h"

namespace
{
bool isDefaultAdapterLuid(varjo_Luid varjoLuid)
{
    using Microsoft::WRL::ComPtr;

    ComPtr<IDXGIFactory> factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter> adapter = nullptr;
        if (SUCCEEDED(factory->EnumAdapters(0, &adapter))) {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                return (desc.AdapterLuid.LowPart == varjoLuid.low) && (desc.AdapterLuid.HighPart == varjoLuid.high);
            }
        }
    }

    printf("Failed to get default adapter luid\n");
    return false;
}

GLuint compileShader(GLenum type, const char* source, const char* name)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint infoSize = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoSize);

        std::vector<char> infoLog(infoSize);
        glGetShaderInfoLog(shader, infoSize, &infoSize, infoLog.data());

        printf("Failed to compile '%s': \n", name);
        printf("%s\n", infoLog.data());
        abort();
    }

    return shader;
}

GLuint linkProgram(const std::vector<GLuint>& shaders, const char* name)
{
    GLuint program = glCreateProgram();
    for (const GLuint shader : shaders) {
        glAttachShader(program, shader);
    }
    glLinkProgram(program);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint infoLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLength);

        std::vector<char> info(infoLength);
        glGetProgramInfoLog(program, infoLength, nullptr, info.data());

        printf("Failed to link %s: %s", name, info.data());
        abort();
    }

    return program;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_DESTROY: PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd, message, wParam, lParam); break;
    }

    return 0;
}

bool hasExtension(const char* name)
{
    GLint numExtensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
    for (int i = 0; i < numExtensions; i++) {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        if (strcmp(name, ext) == 0) {
            return true;
        }
    }
    return false;
}

typedef void(GLAPIENTRY* PFNGLBINDSHADINGRATEIMAGENVPROC)(GLuint texture);
typedef void(GLAPIENTRY* PFNGLSHADINGRATEIMAGEPALETTENVPROC)(GLuint viewport, GLuint first, GLuint count, const GLenum* rates);
typedef void(GLAPIENTRY* PFNGLGETSHADINGRATEIMAGEPALETTENVPROC)(GLuint viewport, GLuint entry, GLenum* rate);
typedef void(GLAPIENTRY* PFNGLSHADINGRATEIMAGEBARRIERNVPROC)(GLboolean synchronize);
typedef void(GLAPIENTRY* PFNGLSHADINGRATESAMPLEORDERCUSTOMNVPROC)(GLenum rate, GLuint samples, const GLint* locations);
typedef void(GLAPIENTRY* PFNGLGETSHADINGRATESAMPLELOCATIONIVNVPROC)(GLenum rate, GLuint index, GLint* location);

PFNGLBINDSHADINGRATEIMAGENVPROC glBindShadingRateImageNV(nullptr);
PFNGLSHADINGRATEIMAGEPALETTENVPROC glShadingRateImagePaletteNV(nullptr);
PFNGLGETSHADINGRATEIMAGEPALETTENVPROC glGetShadingRateImagePaletteNV(nullptr);
PFNGLSHADINGRATEIMAGEBARRIERNVPROC glShadingRateImageBarrierNV(nullptr);
PFNGLSHADINGRATESAMPLEORDERCUSTOMNVPROC glShadingRateSampleOrderCustomNV(nullptr);
PFNGLGETSHADINGRATESAMPLELOCATIONIVNVPROC glGetShadingRateSampleLocationivNV(nullptr);

#define GL_SHADING_RATE_IMAGE_NV 0x9563

#define GL_SHADING_RATE_NO_INVOCATIONS_NV 0x9564
#define GL_SHADING_RATE_1_INVOCATION_PER_PIXEL_NV 0x9565
#define GL_SHADING_RATE_1_INVOCATION_PER_1X2_PIXELS_NV 0x9566
#define GL_SHADING_RATE_1_INVOCATION_PER_2X1_PIXELS_NV 0x9567
#define GL_SHADING_RATE_1_INVOCATION_PER_2X2_PIXELS_NV 0x9568
#define GL_SHADING_RATE_1_INVOCATION_PER_2X4_PIXELS_NV 0x9569
#define GL_SHADING_RATE_1_INVOCATION_PER_4X2_PIXELS_NV 0x956A
#define GL_SHADING_RATE_1_INVOCATION_PER_4X4_PIXELS_NV 0x956B
#define GL_SHADING_RATE_2_INVOCATIONS_PER_PIXEL_NV 0x956C
#define GL_SHADING_RATE_4_INVOCATIONS_PER_PIXEL_NV 0x956D
#define GL_SHADING_RATE_8_INVOCATIONS_PER_PIXEL_NV 0x956E
#define GL_SHADING_RATE_16_INVOCATIONS_PER_PIXEL_NV 0x956F

#define GL_SHADING_RATE_IMAGE_BINDING_NV 0x955B
#define GL_SHADING_RATE_IMAGE_TEXEL_WIDTH_NV 0x955C
#define GL_SHADING_RATE_IMAGE_TEXEL_HEIGHT_NV 0x955D
#define GL_SHADING_RATE_IMAGE_PALETTE_SIZE_NV 0x955E
#define GL_MAX_COARSE_FRAGMENT_SAMPLES_NV 0x955F

#define GL_SHADING_RATE_SAMPLE_ORDER_DEFAULT_NV 0x95AE
#define GL_SHADING_RATE_SAMPLE_ORDER_PIXEL_MAJOR_NV 0x95AF
#define GL_SHADING_RATE_SAMPLE_ORDER_SAMPLE_MAJOR_NV 0x95B0

const int c_defaultPaletteSize = 16;

const GLenum c_nvShadingRates[c_defaultPaletteSize] = {
    GL_SHADING_RATE_16_INVOCATIONS_PER_PIXEL_NV,     //
    GL_SHADING_RATE_8_INVOCATIONS_PER_PIXEL_NV,      //
    GL_SHADING_RATE_4_INVOCATIONS_PER_PIXEL_NV,      //
    GL_SHADING_RATE_2_INVOCATIONS_PER_PIXEL_NV,      //
    GL_SHADING_RATE_1_INVOCATION_PER_PIXEL_NV,       //
    GL_SHADING_RATE_1_INVOCATION_PER_1X2_PIXELS_NV,  //
    GL_SHADING_RATE_1_INVOCATION_PER_2X1_PIXELS_NV,  //
    GL_SHADING_RATE_1_INVOCATION_PER_2X2_PIXELS_NV,  //
    GL_SHADING_RATE_1_INVOCATION_PER_2X4_PIXELS_NV,  //
    GL_SHADING_RATE_1_INVOCATION_PER_4X2_PIXELS_NV,  //
    GL_SHADING_RATE_1_INVOCATION_PER_4X4_PIXELS_NV,  //
    GL_SHADING_RATE_NO_INVOCATIONS_NV,               //
    GL_SHADING_RATE_NO_INVOCATIONS_NV,               //
    GL_SHADING_RATE_NO_INVOCATIONS_NV,               //
    GL_SHADING_RATE_NO_INVOCATIONS_NV,               //
    GL_SHADING_RATE_NO_INVOCATIONS_NV                //
};

void GLAPIENTRY messageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
    (void)source;
    (void)id;
    (void)length;
    (void)userParam;

    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }

    switch (severity) {
        case GL_DEBUG_SEVERITY_LOW: printf("[LOW]"); break;
        case GL_DEBUG_SEVERITY_MEDIUM: printf("[MEDIUM]"); break;
        case GL_DEBUG_SEVERITY_HIGH: printf("[HIGH]"); break;
        case GL_DEBUG_SEVERITY_NOTIFICATION: printf("[NOTIFICATION]"); break;
        default: printf("[UNKNOWN]"); break;
    }

    printf(" ");

    switch (type) {
        case GL_DEBUG_TYPE_ERROR: printf("TYPE_ERROR"); break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: printf("DEPRECATED_BEHAVIOR"); break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: printf("UNDEFINED_BEHAVIOR"); break;
        case GL_DEBUG_TYPE_PORTABILITY: printf("PORTABILITY"); break;
        case GL_DEBUG_TYPE_PERFORMANCE: printf("PERFORMANCE"); break;
        case GL_DEBUG_TYPE_MARKER: printf("MARKER"); break;
        case GL_DEBUG_TYPE_PUSH_GROUP: printf("PUSH_GROUP"); break;
        case GL_DEBUG_TYPE_POP_GROUP: printf("POP_GROUP"); break;
        case GL_DEBUG_TYPE_OTHER: printf("OTHER"); break;
        default: printf("UNKNOWN"); break;
    }

    printf(" %s\n", message);
}
}  // namespace


GLColorRenderTexture::GLColorRenderTexture(int32_t width, int32_t height, GLuint texture)
    : RenderTexture(width, height)
{
    m_colorTexture = GLTextureRef{texture, false};
}

GLColorRenderTexture::GLColorRenderTexture(int32_t width, int32_t height, varjo_TextureFormat format)
    : RenderTexture(width, height)
{
    if (format == varjo_TextureFormat_B8G8R8A8_SRGB) {
        abort();
    }

    GLuint colorBuffer{};
    glGenTextures(1, &colorBuffer);
    glBindTexture(GL_TEXTURE_2D, colorBuffer);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Failed to create GL color buffer: %x", error);
        abort();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    m_colorTexture = GLTextureRef{colorBuffer, true};
}

GLColorRenderTexture::~GLColorRenderTexture()
{
    if (m_colorTexture.owned) glDeleteTextures(1, &m_colorTexture.textureId);
}

GLDepthRenderTexture::GLDepthRenderTexture(int32_t width, int32_t height, GLuint texture, bool hasStencil)
    : RenderTexture(width, height)
{
    m_hasStencil = hasStencil;
    if (!texture) {
        glGenRenderbuffers(1, &m_depthTexture.textureId);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthTexture.textureId);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, m_width, m_height);

        m_depthTexture.owned = true;
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            printf("Failed to create render buffer: %x", error);
            abort();
        }
    } else {
        m_depthTexture = GLTextureRef{texture, false};
    }
}

GLDepthRenderTexture::~GLDepthRenderTexture()
{
    if (m_depthTexture.owned) glDeleteRenderbuffers(1, &m_depthTexture.textureId);
}

GLRenderer::GLRenderer(varjo_Session* session, const RendererSettings& renderer_settings)
    : IRenderer(session, renderer_settings)
{
    if (!isDefaultAdapterLuid(varjo_D3D11GetLuid(session))) {
        printf("Varjo connected adapter is not default.\n");
        exit(EXIT_FAILURE);
    }

    if (renderer_settings.useSli()) {
        _putenv("GL_NV_GPU_MULTICAST=1");
    }

    if (renderer_settings.showMirrorWindow()) {
        glm::ivec2 windowSize = getMirrorWindowSize();
        m_window = std::make_unique<Window>(windowSize.x, windowSize.y, false);
    } else {
        m_window = std::make_unique<Window>(512, 512, true);
    }

    m_hwnd = m_window->getHandle();
    if (!m_hwnd) {
        printf("CreateWindow failed: %s\n", getLastErrorString());
        abort();
    }

    UpdateWindow(m_hwnd);


    if (wglewInit() != GLEW_OK) {
        printf("Failed to initialize WGLEW\n");
        return;
    }

    m_hdc = GetDC(m_hwnd);
    if (m_hdc == NULL) {
        printf("Failed to get DC: %s\n", getLastErrorString());
        return;
    }

    PIXELFORMATDESCRIPTOR pfd = {sizeof(PIXELFORMATDESCRIPTOR), 1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,  //
        PFD_TYPE_RGBA,                                               // RGBA colors.
        32,                                                          // 32 bit framebuffer
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        24,  // Number of bits for the depthbuffer
        8,   // Number of bits for the stencilbuffer
        0,   // Number of Aux buffers in the framebuffer.
        PFD_MAIN_PLANE, 0, 0, 0, 0};

    int pixelFormat = ChoosePixelFormat(m_hdc, &pfd);

    if (SetPixelFormat(m_hdc, pixelFormat, &pfd) == false) {
        printf("Failed to set pixel format: %s", getLastErrorString());
        return;
    }

    m_hglrc = wglCreateContext(m_hdc);
    if (m_hglrc == NULL) {
        printf("Failed to create OpenGL context: %s\n", getLastErrorString());
        return;
    }

    if (wglMakeCurrent(m_hdc, m_hglrc) == false) {
        printf("Failed to set current OpenGL context: %s\n", getLastErrorString());
        return;
    }

    if (glewInit() != GLEW_OK) {
        printf("Failed to initialize GLEW\n");
        return;
    }

#ifdef _DEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(messageCallback, nullptr);
#endif

    if (renderer_settings.useSli() && hasExtension("GL_NV_gpu_multicast")) {
        GLint numGpus{1};
        glGetIntegerv(GL_MULTICAST_GPUS_NV, &numGpus);
        if (numGpus > 1) {
            m_multicast = true;
        }
    }

    compileShaders();
    createInstanceBuffer();
    createPerFrameBuffers();

    // glEnable(GL_FRAMEBUFFER_SRGB); <- Doesn't work when copying textures
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);

    glGenFramebuffers(1, &m_frameBuffer);
}

GLRenderer::~GLRenderer()
{
    glDeleteTextures(1, &m_vrsTexture);
    glDeleteTextures(1, &m_vrsVisualizationTexture);

    freeRendererResources();

    glDeleteProgram(m_program);
    glDeleteProgram(m_gridProgram);
    glDeleteProgram(m_occlusionMeshProgram);
    glDeleteProgram(m_visualizeVrsProgram);

    for (auto& buffer : m_perFrameBuffers) {
        glDeleteBuffers(1, &buffer.uniformBuffer);
    }

    glDeleteBuffers(1, &m_instanceBuffer.buffer);

    if (m_settings.useOcclusionMesh()) {
        glDeleteBuffers(static_cast<GLsizei>(m_occlusionMeshBuffers.size()), m_occlusionMeshBuffers.data());
    }

    glDeleteFramebuffers(1, &m_frameBuffer);

    wglDeleteContext(m_hglrc);
    ReleaseDC(m_hwnd, m_hdc);
    DestroyWindow(m_hwnd);
}

std::shared_ptr<Geometry> GLRenderer::createGeometry(uint32_t vertexCount, uint32_t indexCount)
{
    return std::make_shared<GLGeometry>(vertexCount, indexCount);
}

std::shared_ptr<RenderTexture> GLRenderer::createColorTexture(int32_t width, int32_t height, varjo_Texture colorTexture)
{
    if (m_settings.useVrs() && m_vrsTexture == 0) {
        createVrsResources(width, height);
    }

    m_colorTextureSize = {width, height};

    auto nativeTexture = varjo_ToGLTexture(colorTexture);
    return std::make_shared<GLColorRenderTexture>(width, height, nativeTexture);
}

std::shared_ptr<RenderTexture> GLRenderer::createDepthTexture(int32_t width, int32_t height, varjo_Texture depthTexture)
{
    bool hasStencil = false;
    auto nativeDepthTexure = varjo_ToGLTexture(depthTexture);
    if (nativeDepthTexure) {
        switch (m_depthSwapChainConfig.textureFormat) {
            case varjo_DepthTextureFormat_D32_FLOAT: {
                hasStencil = false;
                break;
            }
            case varjo_DepthTextureFormat_D24_UNORM_S8_UINT: {
                hasStencil = true;
                break;
            }
            case varjo_DepthTextureFormat_D32_FLOAT_S8_UINT: {
                hasStencil = true;
                break;
            }
            default:
                printf("ERROR: Unsupported depth stencil texture format: %d\n", static_cast<int>(m_depthSwapChainConfig.textureFormat));
                abort();
                break;
        }
    }
    return std::make_shared<GLDepthRenderTexture>(width, height, nativeDepthTexure, hasStencil);
}
std::shared_ptr<RenderTexture> GLRenderer::createVelocityTexture(int32_t width, int32_t height, varjo_Texture velocityTexture)
{
    auto nativeTexture = varjo_ToGLTexture(velocityTexture);
    return std::make_shared<GLColorRenderTexture>(width, height, nativeTexture);
}

bool GLRenderer::isVrsSupported() const
{
    return hasExtension("GL_NV_shading_rate_image");  //
}

void GLRenderer::finishRendering() {}

bool GLRenderer::initVarjo()
{
    createSwapchains();

    // Check if the initialization was successful.
    const varjo_Error error = varjo_GetError(m_session);
    if (error != varjo_NoError) {
        printf(varjo_GetErrorDesc(error));
        return false;
    }

    for (uint32_t viewIndex = 0; viewIndex < 2; viewIndex++) {
        createOcclusionMesh(viewIndex);
    }
    return true;
}


varjo_SwapChain* GLRenderer::createSwapChain(varjo_SwapChainConfig2& swapchainConfig) { return varjo_GLCreateSwapChain(m_session, &swapchainConfig); }

void GLRenderer::createSwapchains()
{
    // create color texture swap chain
    m_swapChainConfig.numberOfTextures = 3;
    m_swapChainConfig.textureArraySize = 1;
    m_swapChainConfig.textureFormat = m_settings.noSrgb() ? varjo_TextureFormat_R8G8B8A8_UNORM : varjo_TextureFormat_R8G8B8A8_SRGB;
    m_swapChainConfig.textureWidth = getTotalViewportsWidth();
    m_swapChainConfig.textureHeight = getTotalViewportsHeight();

    m_colorSwapChain = varjo_GLCreateSwapChain(m_session, &m_swapChainConfig);

    if (m_settings.useDepthLayers()) {
        // create depth texture swap chain
        m_depthSwapChainConfig = m_swapChainConfig;
        m_depthSwapChainConfig.textureFormat = m_settings.depthFormat();
        m_depthSwapChain = varjo_GLCreateSwapChain(m_session, &m_depthSwapChainConfig);
    }
    if (m_settings.useVelocity()) {
        // create velocity texture swap chain
        m_velocitySwapChainConfig = m_swapChainConfig;
        m_velocitySwapChainConfig.textureFormat = varjo_VelocityTextureFormat_R8G8B8A8_UINT;
        m_velocitySwapChain = varjo_GLCreateSwapChain(m_session, &m_velocitySwapChainConfig);
    }
}

void GLRenderer::bindRenderTarget(const RenderTargetTextures& renderTarget)
{
    const auto colorTexture = std::static_pointer_cast<GLColorRenderTexture, RenderTexture>(renderTarget.getColorTexture());
    const auto depthTexture = std::static_pointer_cast<GLDepthRenderTexture, RenderTexture>(renderTarget.getDepthTexture());
    const auto velocityTexture = std::static_pointer_cast<GLColorRenderTexture, RenderTexture>(renderTarget.getVelocityTexture());

    glBindFramebuffer(GL_FRAMEBUFFER, m_frameBuffer);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture->backBuffer(), 0);
    if (depthTexture) {
        if (depthTexture->isRenderBuffer()) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthTexture->depthBuffer());
        } else {
            glFramebufferTexture2D(
                GL_FRAMEBUFFER, depthTexture->hasStencil() ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture->depthBuffer(), 0);
        }
    }

    if (velocityTexture) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, velocityTexture->backBuffer(), 0);
    }

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Failed to bind render buffer: %x", error);
        abort();
    }

    GLenum drawBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, drawBuffers);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("Incomplete framebuffer: %x\n", status);
        abort();
    }

    m_currentBackbuffer = colorTexture->backBuffer();
    m_currentRenderTarget = renderTarget;
}

void GLRenderer::unbindRenderTarget() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

void GLRenderer::clearRenderTarget(const RenderTargetTextures& renderTarget, float r, float g, float b, float a)
{
    bindRenderTarget(renderTarget);

    const float color[]{r, g, b, a};
    glClearBufferfv(GL_COLOR, 0, color);
    glClearDepth(1.0);
    glClearStencil(0);
    glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    if (m_settings.useVelocity()) {
        constexpr float zeroVelocity[]{0.0f, 0.0f, 0.0f, 0.0f};
        glClearBufferfv(GL_COLOR, 1, zeroVelocity);
    }

    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Failed to clear framebuffer: %x\n", error);
        abort();
    }
}

void GLRenderer::freeCurrentRenderTarget() { m_currentRenderTarget.reset(); }

void GLRenderer::useGeometry(const std::shared_ptr<Geometry>& geometry)
{
    const auto glGeometry = std::static_pointer_cast<GLGeometry, Geometry>(geometry);

    glBindVertexArray(glGeometry->vao());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glGeometry->indexBuffer());

    m_currentGeometry = geometry;
}

void GLRenderer::setupCamera(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
{
    m_shaderUniforms.viewMatrix = viewMatrix;
    m_shaderUniforms.projectionMatrix = projectionMatrix;
    m_shaderUniforms.viewportSize = {m_currentViewPort.width, m_currentViewPort.height};

    // Update uniforms.
    const GLuint uniformBuffer = m_perFrameBuffers[m_currentFrameBuffer].uniformBuffer;
    if (m_multicast) {
        glMulticastBufferSubDataNV(getGpuMaskForView(m_currentViewIndex), uniformBuffer, 0, sizeof(m_shaderUniforms), &m_shaderUniforms);
    } else {
        glNamedBufferSubData(uniformBuffer, 0, sizeof(m_shaderUniforms), &m_shaderUniforms);
    }

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Failed to update uniform buffer: %x\n", error);
        abort();
    }

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniformBuffer);
    glUniformBlockBinding(m_program, 0, 0);
}

void GLRenderer::setViewport(const varjo_Viewport& viewport)
{
    glScissor(viewport.x, viewport.y, viewport.width, viewport.height);
    glViewport(viewport.x, viewport.y, viewport.width, viewport.height);

    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Failed to set viewport: %x\n", error);
        abort();
    }
    m_currentViewPort = viewport;
}

void GLRenderer::updateVrsMap(const varjo_Viewport& viewport)
{
    varjo_VariableRateShadingConfig config = getDefaultVRSConfig(m_currentViewIndex, viewport, m_vrsTileSize, m_settings, m_renderingGaze);

    const unsigned int textureUi = static_cast<unsigned int>(m_vrsTexture);
    varjo_GLUpdateVariableRateShadingTexture(
        m_session, textureUi, static_cast<int>(m_vrsTextureSize.x), static_cast<int>(m_vrsTextureSize.y), &config, &varjoShadingRateTable);
}

void GLRenderer::preRenderView()
{
    if (m_multicast) {
        glRenderGpuMaskNV(getGpuMaskForView(m_currentViewIndex));
    }
}

void GLRenderer::renderOcclusionMesh()
{
    if (m_settings.useOcclusionMesh() && m_currentViewIndex < 2) {
        glEnable(GL_STENCIL_TEST);
        // Stencil buffer will contain value 1 where application shouldn't render
        renderOcclusionMesh(m_currentViewIndex);

        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);  // Stencil test passed if value from stencil buffer != 1
        glStencilMask(0x0);                   // Don't write to stencil
    }
}

void GLRenderer::postRenderView()
{
    if (m_settings.useOcclusionMesh()) {
        glDisable(GL_STENCIL_TEST);
    }
    if (m_multicast) {
        // Add copies for all views, which got rendered on GPU1
        if (GPUMASK_1 == getGpuMaskForView(m_currentViewIndex)) {
            std::shared_ptr<GLColorRenderTexture> currentColorTarget =
                std::static_pointer_cast<GLColorRenderTexture, RenderTexture>(m_currentRenderTarget.getColorTexture());
            m_pendingCopies.push_back({m_currentViewPort.x, m_currentViewPort.y, m_currentViewPort.x, m_currentViewPort.y, m_currentViewPort.width,
                m_currentViewPort.height, currentColorTarget->backBuffer(), currentColorTarget->backBuffer()});

            if (m_settings.useDepthLayers()) {
                std::shared_ptr<GLDepthRenderTexture> currentDepthTarget =
                    std::static_pointer_cast<GLDepthRenderTexture, RenderTexture>(m_currentRenderTarget.getDepthTexture());

                m_pendingCopies.push_back({m_currentViewPort.x, m_currentViewPort.y, m_currentViewPort.x, m_currentViewPort.y, m_currentViewPort.width,
                    m_currentViewPort.height, currentDepthTarget->depthBuffer(), currentDepthTarget->depthBuffer()});
            }
        }
    }
}

void GLRenderer::postRenderFrame()
{
    if (m_multicast) {
        glRenderGpuMaskNV(GPUMASK_ALL);
        m_pendingCopies.clear();
        glMulticastWaitSyncNV(1, GPUMASK_0);
    }

    if (m_settings.visualizeVrs()) {
        glCopyImageSubData(m_currentBackbuffer, GL_TEXTURE_2D, 0, 0, 0, 0, m_vrsVisualizationTexture, GL_TEXTURE_2D, 0, 0, 0, 0, m_colorTextureSize.x,
            m_colorTextureSize.y, 1);
        glUseProgram(m_visualizeVrsProgram);
        glBindImageTexture(0, m_vrsVisualizationTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
        glBindImageTexture(1, m_vrsTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R8);
        glUniform2f(2, static_cast<float>(m_colorTextureSize.x), static_cast<float>(m_colorTextureSize.y));
        glUniform2f(3, m_vrsTextureSize.x, m_vrsTextureSize.y);
        glDispatchCompute(m_colorTextureSize.x / 8, m_colorTextureSize.y / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        glCopyImageSubData(m_vrsVisualizationTexture, GL_TEXTURE_2D, 0, 0, 0, 0, m_currentBackbuffer, GL_TEXTURE_2D, 0, 0, 0, 0, m_colorTextureSize.x,
            m_colorTextureSize.y, 1);
    }

    if (m_window) {
        m_window->present(m_hdc);
    }
}

GLbitfield GLRenderer::getGpuMaskForView(uint32_t viewIndex)
{
    if (m_settings.useSlaveGpu()) {
        // render all views on slave gpu
        return GPUMASK_1;
    }


    if (viewIndex == 0 || viewIndex == 2) {
        // render left-eye on gpu0
        return GPUMASK_0;
    } else if (viewIndex == 1 || viewIndex == 3) {
        // render right-eye on gpu1
        return GPUMASK_1;
    }

    return GPUMASK_ALL;
}

void GLRenderer::renderOcclusionMesh(uint32_t viewIndex)
{
    if (viewIndex >= m_occlusionMeshBuffers.size() || m_occlusionMeshVertexes[viewIndex] == 0) {
        return;
    }

    glStencilFunc(GL_ALWAYS, 1, 0xFF);  // Write value 1 to stencil buffer
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);  // Don't write to depth map
    glStencilMask(0xFF);    // Enable stencil buffer writes

    GLenum drawBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, drawBuffers);

    glUseProgram(m_occlusionMeshProgram);

    glBindVertexArray(0);  // Make sure we don't have any binded vertex array which we could messup with next GL function
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, m_occlusionMeshBuffers[viewIndex]);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glDrawArrays(GL_TRIANGLES, 0, m_occlusionMeshVertexes[viewIndex]);

    // Unbind everything
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void GLRenderer::drawGrid()
{
    // Set blend mode for background grid
    if (m_settings.useRenderVST()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    GLenum drawBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, drawBuffers);

    glUseProgram(m_gridProgram);
    glUniformBlockBinding(m_gridProgram, 0, 0);
    glDrawElements(GL_TRIANGLES, m_currentGeometry->indexCount(), GL_UNSIGNED_INT, nullptr);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // Reset blend mode
    if (m_settings.useRenderVST()) {
        glDisable(GL_BLEND);
    }
}

void GLRenderer::uploadInstanceBuffer(const std::vector<std::vector<ObjectRenderData>>& matrices)
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

    // Map instance buffer
    m_instanceBuffer.data = reinterpret_cast<ObjectRenderData*>(
        glMapNamedBufferRange(m_instanceBuffer.buffer, 0, sizeof(ObjectRenderData) * m_instanceBuffer.maxInstances, GL_MAP_WRITE_BIT));

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Failed to map instance buffer: %x\n", error);
        abort();
    }

    memcpy(m_instanceBuffer.data, instanceBufferData.data(), sizeof(ObjectRenderData) * instanceBufferData.size());
    glUnmapNamedBuffer(m_instanceBuffer.buffer);

    error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Failed to unmap instance buffer: %x\n", error);
        abort();
    }
}

void GLRenderer::drawObjects(std::size_t objectsIndex)
{
    const auto& drawOffsetCount = m_instanceBuffer.drawsOffsetCount[objectsIndex];
    useInstanceBuffer(drawOffsetCount.first);

    GLenum drawBuffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(m_settings.useVelocity() ? 2 : 1, drawBuffers);

    glUseProgram(m_program);
    glDrawElementsInstanced(GL_TRIANGLES, m_currentGeometry->indexCount(), GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(drawOffsetCount.second));
}

void GLRenderer::drawMirrorWindow()
{
    int32_t index;
    varjo_AcquireSwapChainImage(m_mirrorSwapchain, &index);
    if (varjo_GetError(m_session) == varjo_NoError) {
        const varjo_Texture swapchainTexture = varjo_GetSwapChainImage(m_mirrorSwapchain, index);
        GLuint src = varjo_ToGLTexture(swapchainTexture);

        setViewport({0, 0, m_window->getWidth(), m_window->getHeight()});
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_frameBuffer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        for (int i = 0; i < 2; i++) {
            const varjo_MirrorView& view = m_mirrorViews[i];
            glBlitFramebuffer(view.viewport.x, view.viewport.y, view.viewport.x + view.viewport.width, view.viewport.y + view.viewport.height, view.viewport.x,
                view.viewport.y, view.viewport.x + view.viewport.width, view.viewport.y + view.viewport.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        varjo_ReleaseSwapChainImage(m_mirrorSwapchain);
    }
}

void GLRenderer::advance() { m_currentFrameBuffer = (m_currentFrameBuffer + 1) % m_perFrameBuffers.size(); }

void GLRenderer::compileShaders()
{
    compileShader();
    compileGridShader();
    compileStencilShader();
    if (m_settings.visualizeVrs()) {
        compileVrsVisualizeShader();
    }
}

void GLRenderer::compileShader()
{
    std::string shaderHeader = "#version 430\n";
    if (m_settings.useVelocity()) {
        shaderHeader.append("#define USE_VELOCITY\n");
        shaderHeader.append("#define PRECISION " + std::to_string(c_velocityPrecision) + "\n");
    }

    if (m_settings.noSrgb()) {
        shaderHeader.append("#define DISABLE_GAMMA_CORRECTION\n");
    }

    const char* vertexSource = R"glsl(

        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 2) in vec4 worldMatrix0;
        layout(location = 3) in vec4 worldMatrix1;
        layout(location = 4) in vec4 worldMatrix2;
        layout(location = 5) in vec4 worldMatrix3;
        layout(location = 6) in vec4 nextWorldMatrix0;
        layout(location = 7) in vec4 nextWorldMatrix1;
        layout(location = 8) in vec4 nextWorldMatrix2;
        layout(location = 9) in vec4 nextWorldMatrix3;

        layout(std140, binding = 0) uniform Matrices {
            mat4 viewMatrix;
            mat4 projectionMatrix;
            vec2 viewportSize;
        };

        layout(location = 0) out vec3 vNormal;
        layout(location = 1) out vec2 vVelocity;

        void main() {
            mat4 worldMat = mat4(worldMatrix0, worldMatrix1, worldMatrix2, worldMatrix3);
            vec4 pos = projectionMatrix * viewMatrix * worldMat * vec4(position, 1);

            vNormal = (worldMat * vec4(normal, 0)).xyz;
            gl_Position = pos;

        #ifdef USE_VELOCITY
            mat4 nextWorldMat = mat4(nextWorldMatrix0, nextWorldMatrix1, nextWorldMatrix2, nextWorldMatrix3);
            vec4 nextPos = projectionMatrix * viewMatrix * nextWorldMat * vec4(position, 1);

            vVelocity = ((nextPos.xy / nextPos.w) - (pos.xy / pos.w)) * vec2(0.5f, -0.5f) * viewportSize;
        #endif
        }
    )glsl";

    const char* fragmentSource = R"glsl(

        layout(location = 0) in vec3 vNormal;
        layout(location = 1) in vec2 vVelocity;

        layout(location = 0) out vec4 oColor;

        #ifdef USE_VELOCITY
        layout(location = 1) out uvec4 oVelocity;

        uvec4 packVelocity(vec2 floatingPoint)
        {
            ivec2 fixedPoint = ivec2(floatingPoint * PRECISION);
            uvec2 temp = uvec2(fixedPoint.x & 0xFFFF, fixedPoint.y & 0xFFFF);
            return uvec4(temp.r >> 8, temp.r & 0xFF, temp.g >> 8, temp.g & 0xFF);
        }
        #endif

        void main() {
        #ifdef DISABLE_GAMMA_CORRECTION
            oColor = vec4(vNormal, 1);
        #else
            oColor = vec4(pow(vNormal, vec3(1 / 2.2)), 1);
        #endif

        #ifdef USE_VELOCITY
            oVelocity = packVelocity(vVelocity);
        #endif
        }
    )glsl";

    const GLuint vertexShader = ::compileShader(GL_VERTEX_SHADER, (shaderHeader + vertexSource).c_str(), "vertex shader");
    const GLuint fragmentShader = ::compileShader(GL_FRAGMENT_SHADER, (shaderHeader + fragmentSource).c_str(), "fragment shader");

    m_program = linkProgram({vertexShader, fragmentShader}, "program");
}

void GLRenderer::compileGridShader()
{
    std::string shaderHeader = "#version 430\n";
    if (m_settings.noSrgb()) {
        shaderHeader.append("#define DISABLE_GAMMA_CORRECTION\n");
    }

    const char* vertexSource = R"glsl(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;

        layout(std140, binding = 0) uniform Matrices {
            mat4 viewMatrix;
            mat4 projectionMatrix;
        };

        out vec3 vPosition;

        void main() {
            vPosition = position + 0.5;

            mat4 view = viewMatrix;
            view[3][0] = 0;
            view[3][1] = 0;
            view[3][2] = 0;
            gl_Position = projectionMatrix * view * vec4(position, 1);
        }
    )glsl";

    const char* fragmentSource = R"glsl(
        in vec3 vPosition;

        out vec4 oColor;

        float grid(float coordinate, float size) {
            float inRange = step(0.001, 1.0 - coordinate) * step(0.001, coordinate);
            float x = (coordinate * size);
            return inRange * step(0.25, x - floor(x)) * step(0.25, 1.0 - (x - floor(x)));
        }

        void main() {
            float x = grid(vPosition.x, 4.0f);
            float y = grid(vPosition.y, 4.0f);
            float z = grid(vPosition.z, 4.0f);
            float value = clamp(x + y + z, 0.0, 1.0);

            vec3 color = vec3(step(0.999, vPosition.z) * mix(0.65, 0.427, value));
            color += step(0.999, 1.0 - vPosition.z) * mix(1.0, 0.0, value);

            color += step(0.999, vPosition.x) * mix(vec3(1, 0, 0), vec3(0, 1, 0), value);
            color += step(0.999, 1.0 - vPosition.x) * mix(vec3(1, 0, 1), vec3(0, 0, 1), value);

            color += step(0.999, vPosition.y) * mix(vec3(1, 1, 0), vec3(0, 1, 1), value);
            color += step(0.999, 1.0 - vPosition.y) * mix(vec3(0.25, 0, 0.392), vec3(0, 0.392, 0.129), value);

            x = grid(vPosition.x, 16.0);
            y = grid(vPosition.y, 16.0);
            z = grid(vPosition.z, 16.0);
            value = clamp(x + y + z, 0.0, 1.0);
            float alpha = mix(1, 0, value);
        #ifdef DISABLE_GAMMA_CORRECTION
            oColor = vec4(color*alpha, alpha);
        #else
            oColor = vec4(pow(color*alpha, vec3(1 / 2.2)), alpha);
        #endif
        }
    )glsl";

    const GLuint vertexShader = ::compileShader(GL_VERTEX_SHADER, (shaderHeader + vertexSource).c_str(), "grid vertex shader");
    const GLuint fragmentShader = ::compileShader(GL_FRAGMENT_SHADER, (shaderHeader + fragmentSource).c_str(), "grid fragment shader");

    m_gridProgram = linkProgram({vertexShader, fragmentShader}, "grid program");
    glUseProgram(m_gridProgram);
}

void GLRenderer::compileStencilShader()
{
    const char* fragmentSource = R"glsl(
            #version 330 core
            out vec4 FragColor;

            void main()
            {
                FragColor = vec4(0, 0, 0, 1.0);
            }
    )glsl";

    const char* vertexSource = R"glsl(
            #version 330 core
            layout(location = 0) in vec2 position;

            void main() {
                gl_Position = vec4(position.x, position.y, 0.0f, 1.0f);
            }
    )glsl";

    const GLuint vertexShader = ::compileShader(GL_VERTEX_SHADER, vertexSource, "occlusion stencil vertex shader");
    const GLuint fragmentShader = ::compileShader(GL_FRAGMENT_SHADER, fragmentSource, "occlusion stencil fragment shader");

    m_occlusionMeshProgram = linkProgram({vertexShader, fragmentShader}, "occlusion stencil program");
}

void GLRenderer::compileVrsVisualizeShader()
{
    const char* source =
        R"glsl(
#version 450
layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout (rgba8, binding = 0) uniform image2D colorImage;
layout (r8, binding = 1) readonly uniform image2D vrsImage;

layout (location = 2) uniform vec2 texSize;
layout (location = 3) uniform vec2 vrsTexSize;

vec4 vrsColors[11] = {
    vec4(0, 0, 1, 0.2f),             // 0
    vec4(1, 1, 0, 0.5f),             // 1
    vec4(0,0,0,0),
    vec4(0,0,0,0),
    vec4(0, 0, 1, 0.2f),             // 4
    vec4(0, 1, 0, 0.5f),             // 5
    vec4(0.54f, 0.19f, 0.88f, 0.5f), // 6
    vec4(0, 1, 0, 0.5f),
    vec4(0,0,0,0),
    vec4(0.54f, 0.19f, 0.88f, 0.5f), // 9
    vec4(1, 0, 0, 0.5f),             // 10
};

void main()
{
    vec2 uv = gl_GlobalInvocationID.xy / texSize;
    ivec2 vrsMapPos = ivec2(uv * vrsTexSize);
    vec4 vrsValue = imageLoad(vrsImage, vrsMapPos);
    vec4 vrsColor = vrsColors[uint(vrsValue.x * 255.0)];
    vec4 pixel = imageLoad(colorImage, ivec2(gl_GlobalInvocationID.xy));
    imageStore(colorImage, ivec2(gl_GlobalInvocationID.xy), pixel * vrsColor);
}
)glsl";

    const GLuint computeShader = ::compileShader(GL_COMPUTE_SHADER, source, "vrs visualization shader");
    m_visualizeVrsProgram = linkProgram({computeShader}, "vrs visualization program");
}

void GLRenderer::createPerFrameBuffers()
{
    for (int i = 0; i < 4 * 4; ++i) {
        GLuint uniformBuffer = createUniformBuffer();

        m_perFrameBuffers.push_back({
            uniformBuffer,
        });
    }
}

void GLRenderer::createInstanceBuffer()
{
    const int32_t maxInstances = 5000;
    m_instanceBuffer.maxInstances = maxInstances;

    glGenBuffers(1, &m_instanceBuffer.buffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceBuffer.buffer);
    glBufferStorage(GL_ARRAY_BUFFER, maxInstances * sizeof(ObjectRenderData), nullptr, GL_MAP_WRITE_BIT);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Failed to create instance buffer: %x", error);
        abort();
    }
}

GLuint GLRenderer::createUniformBuffer()
{
    GLuint buffer = 0;
    glCreateBuffers(1, &buffer);
    if (m_multicast) {
        glNamedBufferStorage(buffer, sizeof(m_shaderUniforms), nullptr, GL_DYNAMIC_STORAGE_BIT | GL_PER_GPU_STORAGE_BIT_NV);
    } else {
        glBindBuffer(GL_UNIFORM_BUFFER, buffer);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(m_shaderUniforms), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        printf("Failed to create uniform buffer: %x", error);
        abort();
    }
    return buffer;
}

void GLRenderer::useInstanceBuffer(std::size_t offset)
{
    // Set up vertex attributes for instancing.
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceBuffer.buffer);

    for (int i = 2; i <= 9; i++) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 4, GL_FLOAT, false, sizeof(ObjectRenderData), reinterpret_cast<void*>(offset + sizeof(float) * (i - 2) * 4));
        glVertexAttribDivisor(i, 1);
    }
}

void GLRenderer::createVrsResources(int32_t width, int32_t height)
{
    loadVrsExtension();
    createVrsTextures(width, height);
    createVrsPalette();

    glEnable(GL_SHADING_RATE_IMAGE_NV);
}

void GLRenderer::loadVrsExtension()
{
    glBindShadingRateImageNV = (PFNGLBINDSHADINGRATEIMAGENVPROC)wglGetProcAddress("glBindShadingRateImageNV");
    glShadingRateImagePaletteNV = (PFNGLSHADINGRATEIMAGEPALETTENVPROC)wglGetProcAddress("glShadingRateImagePaletteNV");
    glGetShadingRateImagePaletteNV = (PFNGLGETSHADINGRATEIMAGEPALETTENVPROC)wglGetProcAddress("glGetShadingRateImagePaletteNV");
    glShadingRateImageBarrierNV = (PFNGLSHADINGRATEIMAGEBARRIERNVPROC)wglGetProcAddress("glShadingRateImageBarrierNV");
    glShadingRateSampleOrderCustomNV = (PFNGLSHADINGRATESAMPLEORDERCUSTOMNVPROC)wglGetProcAddress("glShadingRateSampleOrderCustomNV");
    glGetShadingRateSampleLocationivNV = (PFNGLGETSHADINGRATESAMPLELOCATIONIVNVPROC)wglGetProcAddress("glGetShadingRateSampleLocationivNV");

    if (!glBindShadingRateImageNV || !glShadingRateImagePaletteNV || !glGetShadingRateImagePaletteNV || !glShadingRateImageBarrierNV ||
        !glShadingRateSampleOrderCustomNV || !glGetShadingRateSampleLocationivNV) {
        printf("Failed to load VRS functions\n");
        abort();
    }
}

void GLRenderer::createVrsTextures(int32_t width, int32_t height)
{
    GLint shadingRateTexelWidth;
    GLint shadingRateTexelHeight;
    glGetIntegerv(GL_SHADING_RATE_IMAGE_TEXEL_WIDTH_NV, &shadingRateTexelWidth);
    glGetIntegerv(GL_SHADING_RATE_IMAGE_TEXEL_HEIGHT_NV, &shadingRateTexelHeight);
    m_vrsTileSize = shadingRateTexelWidth;
    const GLsizei shadingRateTextureWidth = width / shadingRateTexelWidth;
    const GLsizei shadingRateTextureHeight = height / shadingRateTexelHeight;
    glGenTextures(1, &m_vrsTexture);

    const size_t totalSize = shadingRateTextureWidth * shadingRateTextureHeight;
    std::vector<uint8_t> data(totalSize);
    for (size_t i = 0; i < totalSize; ++i) {
        data[i] = i % 10;
    }

    glBindTexture(GL_TEXTURE_2D, m_vrsTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8UI, shadingRateTextureWidth, shadingRateTextureHeight);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, shadingRateTextureWidth, shadingRateTextureHeight, GL_RED_INTEGER, GL_UNSIGNED_BYTE, data.data());
    glBindShadingRateImageNV(m_vrsTexture);

    m_vrsTextureSize = {shadingRateTextureWidth, shadingRateTextureHeight};

    if (m_settings.visualizeVrs()) {
        glGenTextures(1, &m_vrsVisualizationTexture);
        glBindTexture(GL_TEXTURE_2D, m_vrsVisualizationTexture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
}

void GLRenderer::createVrsPalette()
{
    GLint paletteSize;
    glGetIntegerv(GL_SHADING_RATE_IMAGE_PALETTE_SIZE_NV, &paletteSize);
    std::vector<GLenum> palette;
    for (int i = 0; i < paletteSize; ++i) {
        GLenum value = i < c_defaultPaletteSize ? c_nvShadingRates[i] : GL_SHADING_RATE_1_INVOCATION_PER_PIXEL_NV;
        palette.push_back(value);
    }

    glShadingRateImagePaletteNV(0, 0, paletteSize, palette.data());
}

varjo_ClipRange GLRenderer::getClipRange() const { return varjo_ClipRangeMinusOneToOne; }


void GLRenderer::createOcclusionMesh(uint32_t viewIndex)
{
    if (!m_settings.useOcclusionMesh()) {
        return;
    }
    glGenBuffers(1, &m_occlusionMeshBuffers[viewIndex]);

    varjo_Mesh2Df* occlusionMesh = varjo_CreateOcclusionMesh(m_session, viewIndex, varjo_WindingOrder_CounterClockwise);
    m_occlusionMeshVertexes[viewIndex] = occlusionMesh->vertexCount;

    if (occlusionMesh->vertexCount == 0) {
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_occlusionMeshBuffers[viewIndex]);
    if (varjo_GetError(m_session) != varjo_NoError) {
        printf("Can't create occlusion mesh for view: %d\n", viewIndex);
        return;
    }

    glBufferStorage(GL_ARRAY_BUFFER, sizeof(varjo_Vector2Df) * occlusionMesh->vertexCount, occlusionMesh->vertices, 0);

    varjo_FreeOcclusionMesh(occlusionMesh);
}

void GLRenderer::recreateOcclusionMesh(uint32_t viewIndex)
{
    if (m_settings.useOcclusionMesh() && viewIndex < 2) {
        glDeleteBuffers(1, &m_occlusionMeshBuffers[viewIndex]);
        createOcclusionMesh(viewIndex);
    }
}

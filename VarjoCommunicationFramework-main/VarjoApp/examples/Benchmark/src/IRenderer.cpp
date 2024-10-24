#include <stdlib.h>
#include <stdio.h>
#include <algorithm>
#include <array>
#include <numeric>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <Varjo_layers.h>
#include <Varjo_math.h>

#include "IRenderer.hpp"
#include "GeometryGenerator.hpp"

namespace
{
glm::mat4 doubleMatrixToGLMMatrix(const double* dMatrix)
{
    glm::mat4 result{};
    float* dst = glm::value_ptr(result);

    for (int i = 0; i < 16; ++i) dst[i] = static_cast<float>(dMatrix[i]);

    return result;
}

const char* getErrorMsg(const unsigned long error)
{
    static char buffer[MAX_PATH];

    // Call FormatMessage to get error string from winapi
    LPWSTR _buffer = nullptr;
    DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&_buffer, 0, nullptr);

    if (size > 0) {
        std::vector<char> descBuffer(size * 2);
        size_t convertCount;
        wcstombs_s(&convertCount, descBuffer.data(), size * 2, _buffer, size);
        sprintf_s(buffer, "0x%X: %s", error, descBuffer.data());
    } else {
        sprintf_s(buffer, "0x%X", error);
    }
    LocalFree(_buffer);

    // trim leading newlines, some error messages might contain them
    int i = (int)(strlen(buffer) - 1);
    for (; i >= 0 && (buffer[i] == '\n' || buffer[i] == '\r'); i--) {
        buffer[i] = '\0';
    }

    return buffer;
}

void setToIdentityMatrix(double* m)
{
    memset(m, 0, 16 * sizeof(double));
    m[0] = 1.0;
    m[5] = 1.0;
    m[10] = 1.0;
    m[15] = 1.0;
}

uint32_t getAtlasWidth(const std::vector<varjo_Viewport>& viewports)
{
    if (viewports.size() < 2) {
        return 0;
    }

    uint32_t contexWidth = 0;
    for (size_t i = 0; i < 2; ++i) {
        contexWidth += viewports[i].width;
    }

    uint32_t focusWidth = 0;
    if (viewports.size() == 4) {
        for (size_t i = 2; i < 4; ++i) {
            focusWidth += viewports[i].width;
        }
    }

    return (std::max)(contexWidth, focusWidth);
}

uint32_t getAtlasHeight(const std::vector<varjo_Viewport>& viewports)
{
    if (viewports.empty()) {
        return 0;
    }

    return viewports.back().height + viewports.back().y;
}

}  // namespace

void RenderTargetTextures::reset()
{
    m_colorTexture.reset();
    m_depthTexture.reset();
    m_velocityTexture.reset();
}

void IRenderer::applyObjectVelocity(Object& object, float timeDeltaSec)
{
    if (std::abs(object.velocity.rotationSpeed) <= std::numeric_limits<float>::epsilon()) {
        return;
    }

    glm::quat rot = glm::angleAxis(object.velocity.rotationSpeed * timeDeltaSec, object.velocity.rotationAxis);
    object.orientation = glm::normalize(object.orientation * rot);
}

IRenderer::IRenderer(varjo_Session* session, const RendererSettings& renderer_settings)
    : m_session(session)
    , m_settings(renderer_settings)
{
}

bool IRenderer::init()
{
    m_settings.setUseVrs(m_settings.useVrs() && isVrsSupported());
    m_settings.setVisualizeVrs(m_settings.useVrs() && m_settings.visualizeVrs());

    initViewports();

    // Initialize rendering API specific Varjo graphics API.
    if (!initVarjo()) {
        return false;
    }

    createRenderTargets();

    m_multiprojectionViews.resize(m_viewCount);
    for (varjo_LayerMultiProjView& multiProjView : m_multiprojectionViews) {
        setToIdentityMatrix(multiProjView.projection.value);
        setToIdentityMatrix(multiProjView.view.value);
    }
    if (m_settings.useDepthLayers()) {
        m_extDepthViews.resize(m_viewCount);
    }
    if (m_settings.useVelocity()) {
        m_extVelocityViews.resize(m_viewCount);
    }

    // Cube geometry for the background grid.
    m_cubeGeometry = GeometryGenerator::generateCube(shared_from_this(), 1.0f, 1.0f, 1.0f);

    // MirrorWindow
    if (m_settings.showMirrorWindow()) {
        const varjo_ViewDescription viewDesc = varjo_GetViewDescription(m_session, 0);
        const float aspectRatio = static_cast<float>(viewDesc.width) / viewDesc.height;

        const int width = 512;
        const int height = static_cast<int>(width / aspectRatio);
        varjo_SwapChainConfig2 config{};
        config.numberOfTextures = 3;
        config.textureArraySize = 1;
        config.textureFormat = varjo_TextureFormat_R8G8B8A8_SRGB;
        config.textureWidth = width * 2;
        config.textureHeight = height;
        m_mirrorSwapchain = createSwapChain(config);

        // Side by side, first view left eye, second view right eye
        for (int i = 0; i < 2; i++) {
            varjo_MirrorView mirrorView{};
            mirrorView.viewIndex = i;
            mirrorView.viewport = varjo_SwapChainViewport{m_mirrorSwapchain, i * width, 0, width, height, 0};
            m_mirrorViews.push_back(mirrorView);
        }

        varjo_SetMirrorConfig(m_session, m_mirrorViews.data(), static_cast<uint32_t>(m_mirrorViews.size()));
    }

    return true;
}

std::vector<varjo_Viewport> IRenderer::calculateViewports(varjo_TextureSize_Type type) const
{
    const int32_t viewCount = type == varjo_TextureSize_Type_Stereo ? 2 : varjo_GetViewCount(m_session);
    std::vector<varjo_Viewport> viewports;
    viewports.reserve(viewCount);
    int x = 0, y = 0;
    for (int32_t i = 0; i < viewCount; i++) {
        int32_t width = 0, height = 0;
        varjo_GetTextureSize(m_session, type, i, &width, &height);
        width = (std::min)((std::max)(256, width), 8096);
        height = (std::min)((std::max)(256, height), 8096);
#ifndef _WIN64
        if (type == varjo_TextureSize_Type_Stereo) {
            // Limit texture size to save virtual memory when not on 64bit platform
            width /= 2;
            height /= 2;
        }
#endif
        const varjo_Viewport viewport = varjo_Viewport{x, y, width, height};
        viewports.push_back(viewport);
        x += viewport.width;
        if (i > 0 && viewports.size() % 2 == 0) {
            x = 0;
            y += viewport.height;
        }
    }
    return viewports;
}

const varjo_Viewport& IRenderer::getActiveViewport(int32_t viewIndex) const { return getActiveViewports()[viewIndex]; }

void IRenderer::calculateWorldMatrices(std::vector<IRenderer::ObjectRenderData>& worldMatrices, const std::vector<IRenderer::Object>& objects)
{
    worldMatrices.resize(objects.size());

    const size_t numObjects = objects.size();
    for (size_t i = 0; i < numObjects; ++i) {
        const IRenderer::Object& object = objects[i];

        glm::mat4 matrix = glm::toMat4(object.orientation);
        matrix[3][0] = object.position.x;
        matrix[3][1] = object.position.y;
        matrix[3][2] = object.position.z;
        matrix = glm::scale(matrix, object.scale);

        worldMatrices[i].world = matrix;

        if (m_settings.useVelocity()) {
            IRenderer::Object nextFrameObject = object;
            applyObjectVelocity(nextFrameObject, c_velocityTimeDelta);

            glm::mat4 nextFrameMatrix = glm::toMat4(nextFrameObject.orientation);
            nextFrameMatrix[3][0] = nextFrameObject.position.x;
            nextFrameMatrix[3][1] = nextFrameObject.position.y;
            nextFrameMatrix[3][2] = nextFrameObject.position.z;
            nextFrameMatrix = glm::scale(nextFrameMatrix, nextFrameObject.scale);

            worldMatrices[i].nextFrameWorld = nextFrameMatrix;
        } else {
            worldMatrices[i].nextFrameWorld = matrix;
        }
    }
}

const std::vector<varjo_Viewport>& IRenderer::getActiveViewports() const
{
    if (m_useFoveatedViewports) {
        return m_foveatedViewports;
    }

    return m_viewports;
}

void IRenderer::freeSwapchainsAndRenderTargets()
{
    m_colorTargets.clear();
    m_depthTargets.clear();
    m_velocityTargets.clear();

    varjo_FreeSwapChain(m_colorSwapChain);

    if (m_settings.useDepthLayers()) {
        varjo_FreeSwapChain(m_depthSwapChain);
    }
    if (m_settings.useVelocity()) {
        varjo_FreeSwapChain(m_velocitySwapChain);
    }
    if (m_settings.showMirrorWindow()) {
        varjo_FreeSwapChain(m_mirrorSwapchain);
    }
}

void IRenderer::freeVarjoResources()
{
    freeCurrentRenderTarget();
    freeSwapchainsAndRenderTargets();
}

void IRenderer::freeRendererResources()
{
    m_cubeGeometry.reset();
    m_currentGeometry.reset();
}

void IRenderer::initViewports()
{
    m_viewCount = m_settings.useStereo() ? 2 : varjo_GetViewCount(m_session);
    printf("  View count: %d\n", m_viewCount);

    const varjo_TextureSize_Type type = m_settings.useStereo() ? varjo_TextureSize_Type_Stereo : varjo_TextureSize_Type_Quad;
    m_viewports = calculateViewports(type);

    if (m_settings.useDynamicViewports()) {
        m_foveatedViewports = calculateViewports(varjo_TextureSize_Type_DynamicFoveation);
    }
    printf("  View sizes:\n");
    for (const auto& viewport : m_settings.useDynamicViewports() ? m_foveatedViewports : m_viewports) {
        printf("    {%d x %d}\n", viewport.width, viewport.height);
    }
}

void IRenderer::createRenderTargets()
{
    // Create a render target for each swap chain texture.
    for (int i = 0; i < m_swapChainConfig.numberOfTextures; ++i) {
        const varjo_Texture colorTexture = varjo_GetSwapChainImage(m_colorSwapChain, i);
        m_colorTargets.push_back(createColorTexture(m_swapChainConfig.textureWidth, m_swapChainConfig.textureHeight, colorTexture));

        const varjo_Texture depthTexture = m_settings.useDepthLayers() ? varjo_GetSwapChainImage(m_depthSwapChain, i) : varjo_Texture{0};
        m_depthTargets.push_back(createDepthTexture(m_swapChainConfig.textureWidth, m_swapChainConfig.textureHeight, depthTexture));

        if (m_settings.useVelocity()) {
            const varjo_Texture velocityTexture = varjo_GetSwapChainImage(m_velocitySwapChain, i);
            m_velocityTargets.push_back(createVelocityTexture(m_swapChainConfig.textureWidth, m_swapChainConfig.textureHeight, velocityTexture));
        }
    }
}

uint32_t IRenderer::getTotalViewportsWidth() const { return (std::max)(getAtlasWidth(m_viewports), getAtlasWidth(m_foveatedViewports)); }

uint32_t IRenderer::getTotalViewportsHeight() const { return (std::max)(getAtlasHeight(m_viewports), getAtlasHeight(m_foveatedViewports)); }

void IRenderer::updateViewportLayout()
{
    for (uint32_t i = 0; i < m_viewCount; ++i) {
        const varjo_Viewport& viewport = getActiveViewport(i);

        m_multiprojectionViews[i].viewport = varjo_SwapChainViewport{m_colorSwapChain, viewport.x, viewport.y, viewport.width, viewport.height, 0};

        if (m_settings.useDepthLayers()) {
            m_extDepthViews[i].header.type = varjo_ViewExtensionDepthType;
            m_extDepthViews[i].header.next = nullptr;
            m_extDepthViews[i].minDepth = 0;
            m_extDepthViews[i].maxDepth = 1;
            m_extDepthViews[i].nearZ = m_settings.useReverseDepth() ? c_farClipDistance : c_nearClipDistance;
            m_extDepthViews[i].farZ = m_settings.useReverseDepth() ? c_nearClipDistance : c_farClipDistance;
            m_extDepthViews[i].viewport = varjo_SwapChainViewport{m_depthSwapChain, viewport.x, viewport.y, viewport.width, viewport.height, 0};
        }
        if (m_settings.useVelocity()) {
            m_extDepthViews[i].header.next = &m_extVelocityViews[i].header;

            m_extVelocityViews[i].header.type = varjo_ViewExtensionVelocityType;
            m_extVelocityViews[i].header.next = nullptr;
            m_extVelocityViews[i].includesHMDMotion = varjo_False;
            m_extVelocityViews[i].velocityScale = 1.0 / (c_velocityTimeDelta * c_velocityPrecision);
            m_extVelocityViews[i].viewport = varjo_SwapChainViewport{m_velocitySwapChain, viewport.x, viewport.y, viewport.width, viewport.height, 0};
        }
    }
}

void IRenderer::drawObjects(const std::vector<Object>& objects, std::size_t objectsIndex)
{
    if (!objects.empty()) {
        // Assume all of the objects use the same geometry (for now).
        useGeometry(objects[0].geometry);
        // Queue objects and render
        drawObjects(objectsIndex);
    }
}

void IRenderer::render(varjo_FrameInfo* frameInfo, const std::vector<std::vector<Object>*>& instancedObjects)
{
    // Begin rendering of the frame
    varjo_BeginFrameWithLayers(m_session);

    {
        std::shared_ptr<RenderTexture> colorTexture;
        std::shared_ptr<RenderTexture> depthTexture;
        std::shared_ptr<RenderTexture> velocityTexture;

        int32_t swapChainIndex{};
        int32_t depthSwapChainIndex{};
        varjo_AcquireSwapChainImage(m_colorSwapChain, &swapChainIndex);
        colorTexture = m_colorTargets[swapChainIndex];
        if (m_settings.useDepthLayers()) {
            varjo_AcquireSwapChainImage(m_depthSwapChain, &depthSwapChainIndex);
        } else {
            depthSwapChainIndex = swapChainIndex;
        }
        depthTexture = m_depthTargets[depthSwapChainIndex];

        if (m_settings.useVelocity()) {
            varjo_AcquireSwapChainImage(m_velocitySwapChain, &swapChainIndex);
            velocityTexture = m_velocityTargets[swapChainIndex];
        }
        RenderTargetTextures renderTarget = RenderTargetTextures(colorTexture, depthTexture, velocityTexture);
        bindRenderTarget(renderTarget);

        // Clear the whole texture to black
        setViewport({0, 0, static_cast<int32_t>(getTotalViewportsWidth()), static_cast<int32_t>(getTotalViewportsHeight())});
        clearRenderTarget(renderTarget, 0, 0, 0, 0);
    }

    // Calculate object world matrices.
    m_objectWorldMatrices.resize(instancedObjects.size());
    std::transform(instancedObjects.begin(), instancedObjects.end(), m_objectWorldMatrices.begin(), [this](std::vector<Object>* objects) {
        std::vector<ObjectRenderData> worldMatrices;
        calculateWorldMatrices(worldMatrices, *objects);
        return worldMatrices;
    });

    uploadInstanceBuffer(m_objectWorldMatrices);

    varjo_Gaze gaze{};
    if (varjo_GetRenderingGaze(m_session, &gaze)) {
        m_renderingGaze = gaze;
    } else {
        m_renderingGaze = std::nullopt;
    }

    const bool useFoveation = m_settings.useDynamicViewports() && m_renderingGaze.has_value();
    useFoveatedViewports(useFoveation);

    preRenderFrame();

    // Render all active views.
    for (uint32_t i = 0; i < m_viewCount; ++i) {
        varjo_ViewInfo& view = frameInfo->views[i];
        if (!view.enabled) {
            continue;  // Skip a view if it is not enabled.
        }

        m_currentViewIndex = i;

        // Set up the viewport.
        const varjo_Viewport& viewport = getActiveViewport(i);
        setViewport(viewport);
        preRenderView();

        if (m_settings.useVrs()) {
            updateVrsMap(viewport);
        }

        // Setup the view and projection matrices.
        glm::mat4 viewMatrix = doubleMatrixToGLMMatrix(view.viewMatrix);

        varjo_FovTangents tangents{};
        if (useFoveation) {
            varjo_FoveatedFovTangents_Hints hints{};
            tangents = varjo_GetFoveatedFovTangents(m_session, i, &m_renderingGaze.value(), &hints);
        } else {
            tangents = varjo_GetFovTangents(m_session, i);
        }
        varjo_Matrix varjoProjectionMatrix = varjo_GetProjectionMatrix(&tangents);

        // Change the near and far clip distances
        const double nearPlane = m_settings.useReverseDepth() ? c_farClipDistance : c_nearClipDistance;
        const double farPlane = m_settings.useReverseDepth() ? c_nearClipDistance : c_farClipDistance;

        varjo_UpdateNearFarPlanes(varjoProjectionMatrix.value, getClipRange(), nearPlane, farPlane);

        const glm::mat4 projectionMatrix = doubleMatrixToGLMMatrix(varjoProjectionMatrix.value);

        setupCamera(viewMatrix, projectionMatrix);

        renderOcclusionMesh();
        // Draw the background grid.
        useGeometry(m_cubeGeometry);

        drawGrid();

        for (size_t i = 0; i < instancedObjects.size(); ++i) {
            drawObjects(*instancedObjects[i], i);
        }

        advance();

        // Submit the right render target.
        std::copy(varjoProjectionMatrix.value, varjoProjectionMatrix.value + 16, m_multiprojectionViews[i].projection.value);
        std::copy(view.viewMatrix, view.viewMatrix + 16, m_multiprojectionViews[i].view.value);

        if (m_settings.useDepthLayers()) {
            m_multiprojectionViews[i].extension = &m_extDepthViews[i].header;
        }

        postRenderView();
    }

    if (m_settings.showMirrorWindow()) {
        drawMirrorWindow();
    }

    postRenderFrame();

    // Finish rendering the frame and send the texture to the compositor.
    varjo_LayerFlags flags = varjo_LayerFlagNone;
    if (m_settings.useRenderVST()) {
        flags |= varjo_LayerFlag_BlendMode_AlphaBlend;
    }

    if (useFoveation) {
        flags |= varjo_LayerFlag_Foveated;
    }

    // Enable depth test against VST
    if (m_settings.useDepthTestVST()) {
        flags |= varjo_LayerFlag_DepthTesting;
    }

    // Enable occlusion mesh
    if (m_settings.useOcclusionMesh()) {
        flags |= varjo_LayerFlag_UsingOcclusionMesh;
    }

    updateViewportLayout();
    varjo_LayerMultiProj multiProjectionLayer{
        {varjo_LayerMultiProjType, flags}, varjo_SpaceLocal, static_cast<int32_t>(m_viewCount), m_multiprojectionViews.data()};
    std::array<varjo_LayerHeader*, 1> layers = {&multiProjectionLayer.header};
    varjo_SubmitInfoLayers submitInfoLayers{frameInfo->frameNumber, 0, m_colorSwapChain != nullptr ? 1 : 0, layers.data()};

    unbindRenderTarget();

    varjo_ReleaseSwapChainImage(m_colorSwapChain);
    if (m_settings.useDepthLayers()) {
        varjo_ReleaseSwapChainImage(m_depthSwapChain);
    }
    if (m_settings.useVelocity()) {
        varjo_ReleaseSwapChainImage(m_velocitySwapChain);
    }

    varjo_EndFrameWithLayers(m_session, &submitInfoLayers);
}

void IRenderer::useFoveatedViewports(bool use) { m_useFoveatedViewports = use; }

void IRenderer::recreateSwapchains()
{
    freeSwapchainsAndRenderTargets();

    initViewports();
    createSwapchains();
    createRenderTargets();
}

const char* IRenderer::getLastErrorString()
{
    const DWORD err = GetLastError();
    return err ? getErrorMsg(err) : "";
}

glm::ivec2 IRenderer::getMirrorWindowSize()
{
    const varjo_ViewDescription viewDesc = varjo_GetViewDescription(m_session, 0);
    const float aspectRatio = static_cast<float>(viewDesc.width) / viewDesc.height;
    int windowWidth = 512;
    int windowHeight = static_cast<int>(windowWidth / aspectRatio);
    windowWidth *= 2;
    return glm::ivec2(windowWidth, windowHeight);
}

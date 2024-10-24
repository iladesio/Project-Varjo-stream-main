// Copyright 2019-2021 Varjo Technologies Oy. All rights reserved.

#include "MRScene.hpp"

#include "ExampleShaders.hpp"

// VarjoExamples namespace contains simple example wrappers for using Varjo API features.
// These are only meant to be used in SDK example applications. In your own application,
// use your own production quality integration layer.
using namespace VarjoExamples;

namespace
{
// 1 unit in cubemap corresponds to this many cd/m^2
constexpr double c_nitsPerUnit = 100.0;

// Scene luminance constant to simulate proper lighting.
constexpr double c_sceneLuminance = 196.0 / (3.0 * c_nitsPerUnit);

// Scene dimensions
constexpr float c_cubeSize = 0.30f;
constexpr int c_gridsize = 5;
constexpr float c_gridSpacing = 1.0f;

// Unit vector dimensions
constexpr float c_unitLen = 1.0f;
constexpr float c_unitWidth = 0.01f;

// Object dimensions
constexpr float d = 1.0f;
constexpr float r = d * 0.5f;

// clang-format off

// Vertex data for cube (position + color)
const std::vector<float> c_cubeVertexData = {
    -r, -r, -r,     0, 0, 0,
    -r, -r, r,      0, 0, 1,
    -r, r, -r,      0, 1, 0,
    -r, r, r,       0, 1, 1,
    r, -r, -r,      1, 0, 0,
    r, -r, r,       1, 0, 1,
    r, r, -r,       1, 1, 0,
    r, r, r,        1, 1, 1,
};

// Index data for cube
const std::vector<unsigned int> c_cubeIndexData = {
    0, 2, 1,
    1, 2, 3,
    4, 5, 6,
    5, 7, 6,
    0, 1, 5,
    0, 5, 4,
    2, 6, 7,
    2, 7, 3,
    0, 4, 6,
    0, 6, 2,
    1, 3, 7,
    1, 7, 5,
};

// Vertex data for plane (position + uv)
const std::vector<float> c_planeVertexData = {
    -2*r, -2*r, 0,     0, 1,
    -2*r, 2*r, 0,      0, 0,
    2*r, -2*r, 0,      1, 1,
    2*r, 2*r, 0,       1, 0,
};

// Index data for plane
const std::vector<unsigned int> c_planeIndexData = {
    0, 2, 1,
    1, 2, 3,
};

// clang-format on

}  // namespace

MRScene::MRScene(Renderer& renderer)
    : m_renderer(renderer)
    , m_cubeMesh(renderer.createMesh(c_cubeVertexData, sizeof(float) * 6, c_cubeIndexData, Renderer::PrimitiveTopology::TriangleList))
    , m_cubeShader(renderer.getShaders().createShader(ExampleShaders::ShaderType::RainbowCube))
    , m_cubemapCubeShader(renderer.getShaders().createShader(ExampleShaders::ShaderType::CubemappedCube))
    , m_solidShader(renderer.getShaders().createShader(ExampleShaders::ShaderType::SolidCube))
    , m_texturedPlaneMesh(renderer.createMesh(c_planeVertexData, sizeof(float) * 5, c_planeIndexData, Renderer::PrimitiveTopology::TriangleList))
    , m_texturedPlaneShader(renderer.getShaders().createShader(ExampleShaders::ShaderType::TexturedPlane))
{
    // Allocate objects
    m_cubes.resize(2 * c_gridsize * c_gridsize * c_gridsize);
    m_units.resize(3);
}

void MRScene::updateHdrCubemap(uint32_t resolution, varjo_TextureFormat format, size_t rowPitch, const uint8_t* data)
{
    if (data) {
        // Create cubemap if not created or the resolution has changed.
        if (!m_hdrCubemapTexture || m_hdrCubemapTexture->getSize().x != resolution) {
            m_hdrCubemapTexture = m_renderer.createHdrCubemap(resolution, format);
        }

        if (m_hdrCubemapTexture) {
            m_renderer.updateTexture(m_hdrCubemapTexture.get(), data, rowPitch);
        }
    } else {
        m_hdrCubemapTexture.reset();
    }
}

void MRScene::updateColorFrame(int ch, const glm::ivec2& resolution, varjo_TextureFormat format, size_t rowPitch, const uint8_t* data)
{
    assert(ch >= 0 && ch < static_cast<int>(m_colorFrameTextures.size()));

    auto& colorFrameTexture = m_colorFrameTextures[ch];

    if (data) {
        // Create texture if not created or the resolution has changed.
        if (!colorFrameTexture || colorFrameTexture->getSize() != resolution) {
            colorFrameTexture = m_renderer.createTexture2D(resolution, format);
        }

        if (colorFrameTexture) {
            m_renderer.updateTexture(colorFrameTexture.get(), data, rowPitch);
        }
    } else {
        colorFrameTexture.reset();
    }
}

void MRScene::onUpdate(double frameTime, double deltaTime, int64_t frameCounter, const VarjoExamples::Scene::UpdateParams& updateParams)
{
    // Cast scene update params
    const auto& params = reinterpret_cast<const UpdateParams&>(updateParams);

    // Update your scene here animated by the frame time and count parameters.
    const bool anim = false;
    double animPhase = anim ? (2.0 * frameTime) : 0.0;
    double animScale = anim ? 0.3 : 0.0;

    // Reset exposure gain when the brightness simulation is toggled. This is to ensure that the filter below won't
    // cause any flicker when switching back and forth.
    if (params.cameraParams.simulateBrightness != m_prevSimulateBrightness) {
        m_exposureGain = -1.0f;
    }

    // Calculate exposure gain based on camera parameters, if brightness simulation is enabled.
    if (params.cameraParams.simulateBrightness) {
        const double camLuminance = std::pow(2.0, -params.cameraParams.exposureEV) / params.cameraParams.cameraCalibrationConstant * c_nitsPerUnit;
        float tgtExposureGain = static_cast<float>(camLuminance);

        // Do some simple filtering for exposure gain.
        m_exposureGain = glm::mix(m_exposureGain, tgtExposureGain, m_exposureGain < 0.0f ? 1.0f : 0.5f);
    } else {
        m_exposureGain = 1.0f;
    }

    m_wbNormalization = params.cameraParams.wbNormalizationData;
    m_prevSimulateBrightness = params.cameraParams.simulateBrightness;

    // Scale lighting with scene luminance.
    m_lighting = params.lighting;

    if (params.cameraParams.simulateBrightness) {
        m_lighting.ambientLight *= c_sceneLuminance;
    }

    // Scene grid offsets: X centered, Y on floor, Z in front
    {
        const float cubeOffs = 0.5f * (c_gridSpacing - c_cubeSize);
        const float gridOffs = 0.5f * c_gridsize * c_gridSpacing;
        const float offsX = 0.0f;
        const float offsY = 0.0f + 0.5f * c_cubeSize;
        const float offsZ = 1.0f + 0.5f * c_cubeSize;

        // Render scene to the target using the view information
        size_t i = 0;
        for (int x = 0; x < c_gridsize; x++) {
            for (int y = 0; y < c_gridsize; y++) {
                for (int z = 0; z < c_gridsize; z++) {
                    // Render grid of cubes on both sides
                    for (int zSign = -1; zSign <= 1; zSign += 2) {
                        auto& object = m_cubes[i++];
                        object.pose.position[0] = offsX + c_gridSpacing * ((float)x - (0.5f * (c_gridsize - 1)));
                        object.pose.position[1] = offsY + c_gridSpacing * ((float)y);
                        object.pose.position[2] = zSign * (offsZ + c_gridSpacing * ((float)z));
                        double s = 1.0 + animScale * sin(animPhase + x + y + z);
                        object.pose.scale = {
                            static_cast<float>(c_cubeSize * s),
                            static_cast<float>(c_cubeSize * s),
                            static_cast<float>(c_cubeSize * s),
                        };
                        object.color = {0.5f, 0.5f, 0.5f, 1.0f};
                        object.vtxColorFactor = 1.0f;
                    }
                }
            }
        }
    }

    // Cubemapped cube object
    m_cubemapCube.pose.position = {-1.0, 1.5, 0.0};
    m_cubemapCube.pose.scale = {0.5f, 0.5f, 0.5f};
    m_cubemapCube.color = {1.0f, 1.0f, 1.0f, 1.0f};
    m_cubemapCube.vtxColorFactor = 0.0f;

    // Textured plane objects
    for (size_t ch = 0; ch < m_texturedPlanes.size(); ch++) {
        m_texturedPlanes[ch].pose.position = {-0.75 + ch * 1.5, 1.5, -1.8};
        m_texturedPlanes[ch].pose.scale = {0.5f, 0.5f, 0.5f};
        m_texturedPlanes[ch].color = {1.0f, 1.0f, 1.0f, 1.0f};
        m_texturedPlanes[ch].vtxColorFactor = 0.0f;
    }

    // Render unit vectors to origin
    {
        m_units[0].pose.position = {0.5 * c_unitLen, 0.5 * c_unitWidth, 0.5 * c_unitWidth};
        m_units[0].pose.scale = {c_unitLen, c_unitWidth, c_unitWidth};
        m_units[0].color = {1.0, 0.0, 0.0, 1.0};

        m_units[1].pose.position = {0.5 * c_unitWidth, 0.5 * c_unitLen, 0.5 * c_unitWidth};
        m_units[1].pose.scale = {c_unitWidth, c_unitLen, c_unitWidth};
        m_units[1].color = {0.0, 1.0, 0.0, 1.0};

        m_units[2].pose.position = {0.5 * c_unitWidth, 0.5 * c_unitWidth, 0.5 * c_unitLen};
        m_units[2].pose.scale = {c_unitWidth, c_unitWidth, c_unitLen};
        m_units[2].color = {0.0, 0.0, 1.0, 1.0};
    }

    for (auto& unit : m_units) {
        unit.vtxColorFactor = 0.0;
    }
}

void MRScene::onRender(
    Renderer& renderer, Renderer::ColorDepthRenderTarget& target, int viewIndex, const glm::mat4x4& viewMat, const glm::mat4x4& projMat, void* userData) const
{
    // Bind the cube shader
    renderer.bindShader(*m_cubeShader);

    // Render cubes
    for (const auto& object : m_cubes) {
        // Calculate model transformation
        glm::mat4x4 modelMat(1.0f);
        modelMat = glm::translate(modelMat, object.pose.position);
        modelMat *= glm::toMat4(object.pose.rotation);
        modelMat = glm::scale(modelMat, object.pose.scale);

        ExampleShaders::RainbowCubeConstants constants{};

        constants.vs.transform = ExampleShaders::TransformData(modelMat, viewMat, projMat);
        constants.vs.vtxColorFactor = object.vtxColorFactor;
        constants.vs.objectColor = object.color;
        constants.vs.objectScale = object.pose.scale;

        constants.ps.lighting = m_lighting;
        constants.ps.exposureGain = m_exposureGain;
        constants.ps.wbNormalization = m_wbNormalization;

        renderer.renderMesh(*m_cubeMesh, constants.vs, constants.ps);
    }

    // Render unit vectors
    m_renderer.bindShader(*m_solidShader);

    for (const auto& object : m_units) {
        // Calculate model transformation
        glm::mat4x4 modelMat(1.0f);
        modelMat = glm::translate(modelMat, object.pose.position);
        modelMat *= glm::toMat4(object.pose.rotation);
        modelMat = glm::scale(modelMat, object.pose.scale);

        ExampleShaders::SolidCubeConstants constants{};

        constants.vs.transform = ExampleShaders::TransformData(modelMat, viewMat, projMat);
        constants.vs.vtxColorFactor = object.vtxColorFactor;
        constants.vs.objectColor = object.color;

        renderer.renderMesh(*m_cubeMesh, constants.vs, constants.ps);
    }

    // Render cubemapped cube if HDR cubemap is available.
    if (m_hdrCubemapTexture) {
        renderer.bindShader(*m_cubemapCubeShader);
        renderer.bindTextures({m_hdrCubemapTexture.get()});

        // Calculate model transformation
        glm::mat4x4 modelMat(1.0f);
        modelMat = glm::translate(modelMat, m_cubemapCube.pose.position);
        modelMat *= glm::toMat4(m_cubemapCube.pose.rotation);
        modelMat = glm::scale(modelMat, m_cubemapCube.pose.scale);

        ExampleShaders::CubemappedCubeConstants constants{};

        constants.vs.transform = ExampleShaders::TransformData(modelMat, viewMat, projMat);

        constants.ps.lighting = m_lighting;
        constants.ps.exposureGain = m_exposureGain;
        constants.ps.wbNormalization = m_wbNormalization;

        renderer.renderMesh(*m_cubeMesh, constants.vs, constants.ps);
    }

    // Render textured planes
    for (size_t ch = 0; ch < m_texturedPlanes.size(); ch++) {
        auto& colorFrameTexture = m_colorFrameTextures[ch];
        if (colorFrameTexture) {
            renderer.bindShader(*m_texturedPlaneShader);
            renderer.bindTextures({colorFrameTexture.get()});

            ExampleShaders::TexturedPlaneConstants constants{};

#if 1
            // Calculate model transformation
            glm::mat4x4 modelMat(1.0f);
            modelMat = glm::translate(modelMat, m_texturedPlanes[ch].pose.position);
            modelMat *= glm::toMat4(m_texturedPlanes[ch].pose.rotation);
            modelMat = glm::scale(modelMat, m_texturedPlanes[ch].pose.scale);

            // Render plane objects in the world
            constants.vs.transform = ExampleShaders::TransformData(modelMat, viewMat, projMat);

#else
            // For full view projection
            constants.vs.transform = ExampleShaders::TransformData(glm::mat4x4(1.0f), glm::mat4x4(1.0f), glm::mat4x4(1.0f));
#endif

            constants.ps.colorCorrection = glm::vec4(1.0, 1.0, 1.0, 1.0);

            renderer.renderMesh(*m_texturedPlaneMesh, constants.vs, constants.ps);
        }
    }
}

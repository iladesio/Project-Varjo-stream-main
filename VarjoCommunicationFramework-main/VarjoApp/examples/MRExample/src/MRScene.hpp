// Copyright 2019-2021 Varjo Technologies Oy. All rights reserved.

#pragma once

#include <memory>
#include <array>
#include <vector>

#include "Globals.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"

//! Simple test scene consisting of grid of cubes and unit vectors in origin
class MRScene : public VarjoExamples::Scene
{
public:
    struct UpdateParams : VarjoExamples::Scene::UpdateParams {
        VarjoExamples::ExampleShaders::LightingData lighting{};  //!< Scene lighting params
    };

    //! Constructor
    MRScene(VarjoExamples::Renderer& renderer);

    //! Update HDR cubemap
    void updateHdrCubemap(uint32_t resolution, varjo_TextureFormat format, size_t rowPitch, const uint8_t* data);

    //! Update color frame
    void updateColorFrame(int ch, const glm::ivec2& resolution, varjo_TextureFormat format, size_t rowPitch, const uint8_t* data);

protected:
    //! Update scene animation
    void onUpdate(double frameTime, double deltaTime, int64_t frameCounter, const VarjoExamples::Scene::UpdateParams& params) override;

    //! Render scene to given view
    void onRender(VarjoExamples::Renderer& renderer, VarjoExamples::Renderer::ColorDepthRenderTarget& target, int viewIndex, const glm::mat4x4& viewMat,
        const glm::mat4x4& projMat, void* userData) const override;

private:
    VarjoExamples::Renderer& m_renderer;  //!< Renderer reference

    //! Simple class for storing object data
    struct Object {
        VarjoExamples::ObjectPose pose{};            //!< Object pose
        glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};  //!< Object color + alpha
        float vtxColorFactor = 1.0f;                 //!< Vertex color factor
    };

    VarjoExamples::ExampleShaders::LightingData m_lighting{};                //!< Scene lighting parameters
    float m_exposureGain = -1.0f;                                            //!< Exposure gain for VR content.
    bool m_prevSimulateBrightness = false;                                   //!< If true, brightness simulation was used on the previous frame.
    VarjoExamples::ExampleShaders::WBNormalizationData m_wbNormalization{};  //!< Whitebalance normalization data.

    std::vector<Object> m_cubes;             //!< Cube grid objects
    std::vector<Object> m_units;             //!< Unit vector objects
    Object m_cubemapCube;                    //!< Cubemapped cube
    std::array<Object, 2> m_texturedPlanes;  //!< Textured planes

    std::unique_ptr<VarjoExamples::Renderer::Mesh> m_cubeMesh;              //!< Mesh object instance
    std::unique_ptr<VarjoExamples::Renderer::Shader> m_cubeShader;          //!< Cube shader instance
    std::unique_ptr<VarjoExamples::Renderer::Shader> m_cubemapCubeShader;   //!< Cube shader instance
    std::unique_ptr<VarjoExamples::Renderer::Shader> m_solidShader;         //!< Solid shader instance
    std::unique_ptr<VarjoExamples::Renderer::Texture> m_hdrCubemapTexture;  //!< HDR cubemap texture

    std::array<std::unique_ptr<VarjoExamples::Renderer::Texture>, 2> m_colorFrameTextures;  //!< Color frame textures for stereo views
    std::unique_ptr<VarjoExamples::Renderer::Mesh> m_texturedPlaneMesh;                     //!< Plane mesh object instance
    std::unique_ptr<VarjoExamples::Renderer::Shader> m_texturedPlaneShader;                 //!< Textured plane shader instance
};

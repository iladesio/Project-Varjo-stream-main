#include "VRSHelper.hpp"

varjo_Viewport mapToVrsTexture(const varjo_Viewport& viewport, int32_t vrsTileSize)
{
    return varjo_Viewport{viewport.x / vrsTileSize, viewport.y / vrsTileSize, viewport.width / vrsTileSize, viewport.height / vrsTileSize};
}

varjo_VariableRateShadingConfig getDefaultVRSConfig(
    uint32_t viewIndex, const varjo_Viewport& viewport, int32_t vrsTileSize, const RendererSettings& settings, const std::optional<varjo_Gaze>& renderingGaze)
{
    const varjo_VariableRateShadingFlags stereoFlag = settings.useStereo() ? varjo_VariableRateShadingFlag_Stereo : varjo_VariableRateShadingFlag_None;
    const varjo_VariableRateShadingFlags gazeFlag =
        settings.useGaze() && renderingGaze.has_value() ? varjo_VariableRateShadingFlag_Gaze : varjo_VariableRateShadingFlag_None;

    varjo_VariableRateShadingConfig config{};
    config.viewIndex = viewIndex;
    config.viewport = mapToVrsTexture(viewport, vrsTileSize);
    config.flags = varjo_VariableRateShadingFlag_OcclusionMap | stereoFlag | gazeFlag;
    config.innerRadius = 0.1f;
    config.outerRadius = 0.15f;
    if (settings.useGaze() && renderingGaze.has_value() && renderingGaze.value().status == varjo_GazeStatus_Valid) {
        config.gaze = renderingGaze.value();
    }

    return config;
}

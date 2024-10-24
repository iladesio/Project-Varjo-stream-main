#pragma once

#include "IRenderer.hpp"
#include "Varjo_types.h"

static const int c_shadingRateCount = 16;

static varjo_ShadingRateTable varjoShadingRateTable{{
    varjo_ShadingRate_X16PerPixel,  //
    varjo_ShadingRate_X8PerPixel,   //
    varjo_ShadingRate_X4PerPixel,   //
    varjo_ShadingRate_X2PerPixel,   //
    varjo_ShadingRate_1x1,          //
    varjo_ShadingRate_1x2,          //
    varjo_ShadingRate_2x1,          //
    varjo_ShadingRate_2x2,          //
    varjo_ShadingRate_2x4,          //
    varjo_ShadingRate_4x2,          //
    varjo_ShadingRate_4x4,          //
    varjo_ShadingRate_Cull,         //
    varjo_ShadingRate_Cull,         //
    varjo_ShadingRate_Cull,         //
    varjo_ShadingRate_Cull,         //
    varjo_ShadingRate_Cull          //
}};

varjo_Viewport mapToVrsTexture(const varjo_Viewport& viewport, int32_t vrsTileSize);

varjo_VariableRateShadingConfig getDefaultVRSConfig(
    uint32_t viewIndex, const varjo_Viewport& viewport, int32_t vrsTileSize, const RendererSettings& settings, const std::optional<varjo_Gaze>& renderingGaze);
#pragma once

#include "IRenderer.hpp"

class GeometryGenerator
{
public:
    static std::shared_ptr<Geometry> generateCube(std::shared_ptr<IRenderer> renderer, float width, float height, float depth);
    static std::shared_ptr<Geometry> generateDonut(std::shared_ptr<IRenderer> renderer, float radius, float thickness, int32_t segments, int32_t tessellation);
};

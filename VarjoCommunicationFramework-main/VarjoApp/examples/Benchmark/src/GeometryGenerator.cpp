#define NOMINMAX
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#define GLM_ENABLE_EXPERIMENTAL

#include <cmath>
#include <algorithm>
#include <fstream>
#include <string>
#include <glm/vec2.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "GeometryGenerator.hpp"

void writeOBJ(const std::string& fileName, const std::vector<Geometry::Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    std::ofstream file(fileName);

    for (auto& vertex : vertices) {
        file << "v " << vertex.position.x << " " << vertex.position.y << " " << vertex.position.z << "\n";
        file << "vn " << vertex.normal.x << " " << vertex.normal.y << " " << vertex.normal.z << "\n";
    }

    for (size_t i = 0; i < indices.size(); i += 3) {
        int32_t i0 = indices[i + 0] + 1;
        int32_t i1 = indices[i + 1] + 1;
        int32_t i2 = indices[i + 2] + 1;

        file << "f "                     //
             << i0 << "//" << i0 << " "  //
             << i1 << "//" << i1 << " "  //
             << i2 << "//" << i2 << "\n";
    }
}

std::shared_ptr<Geometry> GeometryGenerator::generateCube(std::shared_ptr<IRenderer> renderer, float width, float height, float depth)
{
    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;
    float halfDepth = depth * 0.5f;

    std::vector<Geometry::Vertex> vertices = {
        {{-halfWidth, halfHeight, -halfDepth}, {0, -1, 0}},
        {{halfWidth, halfHeight, -halfDepth}, {0, -1, 0}},
        {{halfWidth, halfHeight, halfDepth}, {0, -1, 0}},
        {{-halfWidth, halfHeight, halfDepth}, {0, -1, 0}},

        {{-halfWidth, -halfHeight, -halfDepth}, {0, -1, 0}},
        {{halfWidth, -halfHeight, -halfDepth}, {0, -1, 0}},
        {{halfWidth, -halfHeight, halfDepth}, {0, -1, 0}},
        {{-halfWidth, -halfHeight, halfDepth}, {0, -1, 0}},
    };

    std::vector<uint32_t> indices = {
        0, 1, 2, 0, 2, 3,  // Top
        6, 5, 4, 7, 6, 4,  // Bottom
        0, 3, 7, 0, 7, 4,  // Left
        2, 1, 5, 2, 5, 6,  // Right
        3, 2, 6, 3, 6, 7,  // Front
        1, 0, 4, 1, 4, 5,  // Back
    };

    // writeOBJ("cube.obj", vertices, indices);

    std::shared_ptr<Geometry> geometry = renderer->createGeometry(static_cast<uint32_t>(vertices.size()), static_cast<uint32_t>(indices.size()));
    geometry->updateVertexBuffer(vertices.data());
    geometry->updateIndexBuffer(indices.data());
    return geometry;
}

std::shared_ptr<Geometry> GeometryGenerator::generateDonut(
    std::shared_ptr<IRenderer> renderer, float radius, float thickness, int32_t segments, int32_t tessellation)
{
    segments = std::max(3, segments);
    tessellation = std::max(3, tessellation);

    uint32_t vertexCount = segments * tessellation;
    uint32_t triangleCount = vertexCount * 2;
    uint32_t indexCount = triangleCount * 3;

    std::vector<Geometry::Vertex> vertices(vertexCount);
    std::vector<uint32_t> indices(indexCount);

    std::vector<Geometry::Vertex> surfaceVertices(tessellation);
    std::vector<uint32_t> surfaceIndices(tessellation * 2 * 3);

    float halfThickness = thickness * 0.5f;
    float stepAngle = static_cast<float>(M_PI * 2.0f) / static_cast<float>(tessellation);
    float angle = 0.0f;

    for (int i = 0; i < tessellation; ++i) {
        Geometry::Vertex vertex{{0.0f, 0.0f, halfThickness}, {0.0f, 0.0f, 1.0f}};
        vertex.position = glm::rotateX(vertex.position, angle);
        vertex.normal = glm::normalize(glm::rotateX(vertex.normal, angle));

        surfaceVertices[i] = vertex;
        angle += stepAngle;
    }

    int32_t index = 0;
    for (int i = 0; i < tessellation; ++i) {
        int i0 = i;
        int i1 = (i + 1) < tessellation ? i + 1 : 0;

        surfaceIndices[index++] = i1;
        surfaceIndices[index++] = i0 + tessellation;
        surfaceIndices[index++] = i0;

        surfaceIndices[index++] = i1;
        surfaceIndices[index++] = i1 + tessellation;
        surfaceIndices[index++] = i0 + tessellation;
    }

    stepAngle = static_cast<float>(M_PI * 2.0f) / static_cast<float>(segments);
    angle = 0.0f;

    for (int i = 0; i < segments; ++i) {
        int32_t index = i * tessellation;

        for (int ti = 0; ti < tessellation; ++ti) {
            Geometry::Vertex vertex = surfaceVertices[ti];

            vertex.position.z += radius - halfThickness;
            vertex.position = glm::rotateY(vertex.position, angle);
            vertex.normal = glm::normalize(glm::rotateY(vertex.normal, angle));

            vertices[index + ti] = vertex;
        }

        angle += stepAngle;
    }

    index = 0;
    for (int i = 0; i < segments; ++i) {
        int32_t offset = i * tessellation;
        uint32_t max = (segments * tessellation);

        for (uint32_t idx : surfaceIndices) {
            idx += offset;
            if (idx >= max) {
                idx -= max;
            }
            indices[index++] = idx;
        }
    }

    // writeOBJ("donut.obj", vertices, indices);
    printf("Generated donut %d/%d\n", segments, tessellation);

    std::shared_ptr<Geometry> geometry = renderer->createGeometry(vertexCount, indexCount);
    geometry->updateVertexBuffer(vertices.data());
    geometry->updateIndexBuffer(indices.data());
    return geometry;
}

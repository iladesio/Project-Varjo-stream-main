// Copyright 2022 Varjo Technologies Oy. All rights reserved.

#include "Undistorter.hpp"

#include <glm/gtx/matrix_operation.hpp>

namespace
{
// Get NDC coordinate for pixel coordinate using given viewport size
glm::vec2 pixelToNDC(const glm::ivec2& pixel, const glm::ivec2& viewportSize)
{
    return glm::vec2((glm::vec2(pixel) + 0.5f) / glm::vec2(viewportSize) * glm::vec2(2.0f, -2.0f) + glm::vec2(-1.0f, 1.0f));
}

// Get direction vector from NDC coordinate using given projection
glm::vec3 getViewDir(const glm::vec2& ndcCoord, const glm::mat4x4& inverseProjection)
{
    const glm::vec4 dispCoordStart = glm::vec4(ndcCoord, -0.5f, 1.0f);
    const glm::vec4 dispCoordEnd = glm::vec4(ndcCoord, 0.5f, 1.0f);

    const glm::vec4 viewPosStart = inverseProjection * dispCoordStart;
    const glm::vec4 viewPosEnd = inverseProjection * dispCoordEnd;

    return glm::normalize(glm::vec3(viewPosEnd) / viewPosEnd.w - glm::vec3(viewPosStart) / viewPosStart.w);
}

// Get sample coordinate using omnidir model
glm::vec2 getSampleCoordOmniDir(const varjo_CameraIntrinsics& intrinsics, const glm::vec3& dir, const float srcAspect)
{
    // Extract omnidir specific parameters from distortionCoefficients.
    // Radial distortion parameters.
    const glm::vec2 K(intrinsics.distortionCoefficients[0], intrinsics.distortionCoefficients[1]);
    const float skew = static_cast<float>(intrinsics.distortionCoefficients[2]);
    const float xi = static_cast<float>(intrinsics.distortionCoefficients[3]);
    // Tangential distortion parameters
    const glm::vec2 P(intrinsics.distortionCoefficients[4], intrinsics.distortionCoefficients[5]);
    const glm::vec2 F(intrinsics.focalLengthX, intrinsics.focalLengthY);
    const glm::vec2 C(intrinsics.principalPointX, intrinsics.principalPointY);

    const glm::vec2 V(dir.x / (dir.z + xi), dir.y / (dir.z + xi));
    const float r2 = glm::dot(V, V);
    const float r4 = r2 * r2;

    const float radialDistortion = glm::dot(K, glm::vec2(r2, r4));
    const float Vxy2 = 2.0f * V.x * V.y;
    const glm::vec2 tangentialDistortion = glm::vec2(P.y * (r2 + 2.0f * V.x * V.x) + P.x * Vxy2, P.x * (r2 + 2.0f * V.y * V.y) + P.y * Vxy2);

    // Apply radial and tangential distortion.
    const glm::vec2 xyD = V * (1.0f + radialDistortion) + tangentialDistortion;

    glm::vec2 U = xyD * F + C;
    // Apply skew.
    U.x += skew * xyD.y;
    U.y = ((U.y - 0.5f) / srcAspect) + 0.5f;

    return U;
}
}  // namespace

namespace VarjoExamples
{
Undistorter::Undistorter(const glm::ivec2& inputSize, const glm::ivec2& outputSize, const varjo_CameraIntrinsics& intrinsics, const varjo_Matrix& extrinsics,
    std::optional<const varjo_Matrix> projection)
    : m_inputSize(inputSize)
    , m_outputSize(outputSize)
    , m_intrinsics(intrinsics)
    , m_extrinsicsRotation(glm::mat3x3(VarjoExamples::fromVarjoMatrix(extrinsics)))  // Camera position is not needed for rectification.
{
    if (projection.has_value()) {
        // Store inverse of given projection matrix
        m_inverseProjection = glm::inverse(VarjoExamples::fromVarjoMatrix(projection.value()));
    } else {
        // Offset the projection so that the camera principal point is in the center of projection.
        const glm::mat4x4 offsetMtx = glm::translate(glm::diagonal4x4(glm::vec4{1.0, 1.0, 1.0, 1.0}),
            glm::vec3((intrinsics.principalPointX - 0.5) * 2.0, (0.5 - intrinsics.principalPointY) * 2.0, 0.f));
        const glm::mat4x4 proj =
            offsetMtx * glm::perspectiveFov(glm::radians(80.0f), static_cast<float>(m_inputSize.x), static_cast<float>(m_inputSize.y), 0.001f, 10.f);
        m_inverseProjection = glm::inverse(proj);
    }
}

glm::ivec2 Undistorter::getSampleCoord(int x, int y) const
{
    // Camera and view coordinate systems have opposite YZ direction.
    const glm::mat3x3 flipYZ = glm::diagonal3x3(glm::vec3{1.0, -1.0, -1.0});

    const glm::vec2 ndcCoord = pixelToNDC(glm::ivec2(x, y), m_outputSize);
    const glm::vec3 viewRayDir = getViewDir(ndcCoord, m_inverseProjection);
    const glm::vec3 cameraRayDir = m_extrinsicsRotation * flipYZ * viewRayDir;
    const glm::vec2 sampleCoord = getSampleCoordOmniDir(m_intrinsics, cameraRayDir, static_cast<float>(m_inputSize.x) / m_inputSize.y);

    return sampleCoord * glm::vec2(m_inputSize);
}

}  // namespace VarjoExamples

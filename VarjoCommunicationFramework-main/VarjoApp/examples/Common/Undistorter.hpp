// Copyright 2022 Varjo Technologies Oy. All rights reserved.

#include "Globals.hpp"

#include <optional>

namespace VarjoExamples
{
// NOTICE! This CPU code is just for example, in real case, you'll probably want to do this on GPU.
// The same functionality can be achieved by using OpenCV omnidir module.

//! Example helper class for undistorting the camera images.
class Undistorter
{
public:
    //! Constructor
    Undistorter(const glm::ivec2& inputSize, const glm::ivec2& outputSize, const varjo_CameraIntrinsics& intrinsics, const varjo_Matrix& extrinsics,
        std::optional<const varjo_Matrix> projection);

    //! Get undistorted sample coordinate into distorted source buffer, that should be used for a screen space pixel x,y.
    glm::ivec2 getSampleCoord(int x, int y) const;

private:
    glm::ivec2 m_inputSize;               //!< Input buffer dimenions
    glm::ivec2 m_outputSize;              //!< Output buffer dimenions
    glm::mat4x4 m_inverseProjection;      //!< Inverse projection matrix
    glm::mat3x3 m_extrinsicsRotation;     //!< Camera extrinsics
    varjo_CameraIntrinsics m_intrinsics;  //!< Camera intrinsics
};

}  // namespace VarjoExamples

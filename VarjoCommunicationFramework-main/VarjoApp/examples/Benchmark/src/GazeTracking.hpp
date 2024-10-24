#pragma once

#include <glm/glm.hpp>
#include <Varjo.h>

class GazeTracking
{
public:
    GazeTracking(varjo_Session* session);

    void init();
    void requestCalibration();

    bool update();
    glm::vec3 getPosition() const { return m_position; }

protected:
    varjo_Session* m_session = nullptr;
    bool m_initialized = false;
    glm::dvec3 m_position = {0, 0, 0};
    bool m_calibrating = false;
    bool m_calibrated = false;
};

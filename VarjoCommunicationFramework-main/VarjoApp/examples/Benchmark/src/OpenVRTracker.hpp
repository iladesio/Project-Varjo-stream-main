#pragma once

#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>
#include <string>
#include <cstdio>
#include <Windows.h>
#include <openvr.h>

enum ControllerType {
    Left = 0,
    Right = 1,
    Total = 2,
};

struct Controller {
    vr::VRControllerState_t controllerState;
    vr::TrackedDevicePose_t pose;
    glm::vec3 position;
    glm::quat orientation;
    ControllerType role;
};

class OpenVRTracker
{
public:
    OpenVRTracker();
    ~OpenVRTracker();

    void init();
    void update();
    void exit();

    int getControllerCount() const { return static_cast<int>(m_handControllers.size()); }
    glm::vec3 getControllerPosition(int index) const { return m_handControllers.at(index).position; }
    glm::quat getControllerOrientation(int index) const { return m_handControllers.at(index).orientation; }
    bool isLeftController(int index) const { return m_handControllers.at(index).role == ControllerType::Left; }
    bool isButtonPressed(int index) const { return !!m_handControllers.at(index).controllerState.ulButtonPressed; }
    bool isButtonTouched(int index) const { return !!m_handControllers.at(index).controllerState.ulButtonTouched; }

private:
    vr::IVRSystem* m_vrSystem{nullptr};
    const int c_maxControllers{ControllerType::Total};
    std::vector<Controller> m_handControllers;
};

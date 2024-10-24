
#include "OpenVRTracker.hpp"

// Get the quaternion representing the orientation
glm::quat getOrientation(const vr::HmdMatrix34_t matrix)
{
    glm::quat q;

    q.w = static_cast<float>(sqrt(fmax(0, 1 + matrix.m[0][0] + matrix.m[1][1] + matrix.m[2][2])) / 2);
    q.x = static_cast<float>(sqrt(fmax(0, 1 + matrix.m[0][0] - matrix.m[1][1] - matrix.m[2][2])) / 2);
    q.y = static_cast<float>(sqrt(fmax(0, 1 - matrix.m[0][0] + matrix.m[1][1] - matrix.m[2][2])) / 2);
    q.z = static_cast<float>(sqrt(fmax(0, 1 - matrix.m[0][0] - matrix.m[1][1] + matrix.m[2][2])) / 2);
    q.x = static_cast<float>(copysign(q.x, matrix.m[2][1] - matrix.m[1][2]));
    q.y = static_cast<float>(copysign(q.y, matrix.m[0][2] - matrix.m[2][0]));
    q.z = static_cast<float>(copysign(q.z, matrix.m[1][0] - matrix.m[0][1]));
    return q;
}

// Get the vector representing the position
glm::vec3 getPosition(const vr::HmdMatrix34_t matrix)
{
    glm::vec3 vector;
    vector.x = matrix.m[0][3];
    vector.y = matrix.m[1][3];
    vector.z = matrix.m[2][3];
    return vector;
}

OpenVRTracker::OpenVRTracker() {}

OpenVRTracker::~OpenVRTracker() { exit(); }

void OpenVRTracker::exit()
{
    if (m_vrSystem) {
        vr::VR_Shutdown();
        m_vrSystem = nullptr;
    }
}

void OpenVRTracker::init()
{
    if (m_vrSystem) return;

    if (!vr::VR_IsRuntimeInstalled()) {
        OutputDebugStringA("No runtime installed\n");
        return;
    }

    if (!vr::VR_IsHmdPresent()) {
        OutputDebugStringA("No Hmd Present\n");
        return;
    }

    vr::EVRInitError evrInitError = vr::EVRInitError::VRInitError_None;
    m_vrSystem = vr::VR_Init(&evrInitError, vr::EVRApplicationType::VRApplication_Other, nullptr);

    if (evrInitError != vr::EVRInitError::VRInitError_None) {
        OutputDebugStringA("Failed to initialize openVR\n");
        m_vrSystem = nullptr;
    } else {
        OutputDebugStringA("OpenVR Initialized.\n");
    }
}

void OpenVRTracker::update()
{
    m_handControllers.clear();

    if (!m_vrSystem) return;

    for (vr::TrackedDeviceIndex_t unDevice = 0; unDevice < vr::k_unMaxTrackedDeviceCount; unDevice++) {
        if (!m_vrSystem->IsTrackedDeviceConnected(unDevice)) continue;
        if (vr::VRSystem()->GetTrackedDeviceClass(unDevice) != vr::ETrackedDeviceClass::TrackedDeviceClass_Controller) continue;

        vr::TrackedDevicePose_t trackedDevicePose;
        vr::VRControllerState_t controllerState;
        vr::VRSystem()->GetControllerStateWithPose(vr::TrackingUniverseStanding, unDevice, &controllerState, sizeof(controllerState), &trackedDevicePose);

        vr::ETrackedControllerRole role = vr::VRSystem()->GetControllerRoleForTrackedDeviceIndex(unDevice);

        if (trackedDevicePose.bDeviceIsConnected && trackedDevicePose.bPoseIsValid &&
            trackedDevicePose.eTrackingResult == vr::ETrackingResult::TrackingResult_Running_OK &&
            role != vr::ETrackedControllerRole::TrackedControllerRole_Invalid) {
            Controller handController;
            handController.pose = trackedDevicePose;
            handController.role = (role == vr::ETrackedControllerRole::TrackedControllerRole_LeftHand) ? ControllerType::Left : ControllerType::Right;
            handController.controllerState = controllerState;
            handController.position = getPosition(trackedDevicePose.mDeviceToAbsoluteTracking);
            handController.orientation = getOrientation(trackedDevicePose.mDeviceToAbsoluteTracking);
            m_handControllers.push_back(handController);
        }

        if (m_handControllers.size() == c_maxControllers) return;
    }
}
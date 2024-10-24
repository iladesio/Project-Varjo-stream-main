#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "GazeTracking.hpp"

namespace
{
std::string qualityScoreToString(varjo_GazeEyeCalibrationQuality score)
{
    switch (score) {
        case varjo_GazeEyeCalibrationQuality_Invalid: return "invalid";
        case varjo_GazeEyeCalibrationQuality_Low: return "low";
        case varjo_GazeEyeCalibrationQuality_Medium: return "medium";
        case varjo_GazeEyeCalibrationQuality_High: return "high";
        default: break;
    }

    return "unknown";  // should never happen
}

// Returns current time from system clock with milliseconds as string
std::string getCurrentTimestamp()
{
    const auto currentTime = std::chrono::system_clock::now();
    const auto millisecPart = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime.time_since_epoch()) % 1000;
    const std::time_t t = std::chrono::system_clock::to_time_t(currentTime);
    std::tm bt{};
    localtime_s(&bt, &t);  // secure alternative to std::localtime() by Microsoft
    char buf[80];
    std::strftime(buf, sizeof(buf), "%F %H:%M:%S", &bt);
    std::stringstream str;
    str << buf << "." << std::setfill('0') << std::setw(3) << millisecPart.count();
    return str.str();
}
}  // namespace

GazeTracking::GazeTracking(varjo_Session* session)
    : m_session(session)
{
}

void GazeTracking::init()
{
    if (m_initialized) return;

    // Check that gaze tracking is allowed
    if (!varjo_IsGazeAllowed(m_session)) {
        printf("Gaze tracking is not allowed!\n");
        return;
    }

    // Initialize gaze tracking
    varjo_GazeInit(m_session);
    varjo_Error err = varjo_GetError(m_session);
    if (err != varjo_NoError) {
        printf("Failed to initialize Gaze: %s", varjo_GetErrorDesc(err));
        return;
    }

    m_initialized = true;
}

void GazeTracking::requestCalibration()
{
    if (!m_initialized) return;

    // Request gaze calibration
    const std::string timestamp = getCurrentTimestamp();
    printf("[%s] Gaze calibration requested\n", timestamp.c_str());
    varjo_RequestGazeCalibration(m_session);
}

bool GazeTracking::update()
{
    if (!m_initialized) return false;

    varjo_SyncProperties(m_session);

    const bool calibrating = varjo_GetPropertyBool(m_session, varjo_PropertyKey_GazeCalibrating);
    if (m_calibrating != calibrating) {
        m_calibrating = calibrating;
        const std::string timestamp = getCurrentTimestamp();
        printf("[%s] Gaze is calibrating: %s\n", timestamp.c_str(), (m_calibrating ? "true" : "false"));
    }

    const bool calibrated = varjo_GetPropertyBool(m_session, varjo_PropertyKey_GazeCalibrated);
    if (m_calibrated != calibrated) {
        m_calibrated = calibrated;
        const std::string timestamp = getCurrentTimestamp();
        printf("[%s] Gaze calibrated: %s\n", timestamp.c_str(), (m_calibrated ? "true" : "false"));

        const varjo_GazeEyeCalibrationQuality scoreLeft = varjo_GetPropertyInt(m_session, varjo_PropertyKey_GazeEyeCalibrationQuality_Left);
        printf("Gaze left eye calibration quality score: %s\n", qualityScoreToString(scoreLeft).c_str());

        const varjo_GazeEyeCalibrationQuality scoreRight = varjo_GetPropertyInt(m_session, varjo_PropertyKey_GazeEyeCalibrationQuality_Right);
        printf("Gaze right eye calibration quality score: %s\n", qualityScoreToString(scoreRight).c_str());
    }

    // Get gaze and check that it is valid
    varjo_Gaze gaze = varjo_GetGaze(m_session);
    if (gaze.status == varjo_GazeStatus_Invalid) return false;

    // Calculate relative gaze vector
    glm::vec3 dir = glm::make_vec3(gaze.gaze.forward);
    dir *= gaze.focusDistance;  // Position it on distance where user looks at

    // In gaze coordinates positive Z is in front of user, flip Z axis for this environment
    dir.z *= -1.0f;


    // Position gaze in relative to user pose
    varjo_Matrix m = varjo_FrameGetPose(m_session, varjo_PoseType_Center);
    glm::mat4 mat{glm::make_mat4(m.value)};

    // Calculate gaze position in world coordinates
    m_position = glm::vec3(mat * glm::vec4(dir, 1));
    return true;
}

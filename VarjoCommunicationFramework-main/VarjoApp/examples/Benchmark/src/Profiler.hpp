#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>

class Profiler
{
public:
    void addSample()
    {
        if (!m_started) {
            m_startTime = getCurrentTime();
            m_started = true;
        } else {
            double endTime = getCurrentTime();
            double elapsed = endTime - m_startTime;
            m_startTime = endTime;

            m_frameTimes.push_back(elapsed);
        }
    }

    int32_t sampleCount() const { return static_cast<int32_t>(m_frameTimes.size()); }

    void exportCSV(const std::string& fileName)
    {
        std::ofstream file(fileName);
        if (!file.is_open()) {
            printf("Profiler::exportCSV: Failed to open %s\n", fileName.c_str());
            return;
        }

        size_t frameCount = m_frameTimes.size();
        for (size_t i = 0; i < frameCount; ++i) {
            file << i + 1 << "," << m_frameTimes[i] << "\n";
        }
    }

    using FpsClock = std::chrono::high_resolution_clock;
    void updateFps()
    {
        const std::chrono::seconds fpsInterval{2};

        const auto nowTime = FpsClock::now();
        const auto fpsDuration = nowTime - m_fpsStats.startTime;
        m_fpsStats.frameCount++;
        if (fpsDuration > fpsInterval) {
            double fps = (1000000000.0 * m_fpsStats.frameCount) / fpsDuration.count();
            printf("FPS=%.3f\n", fps);
            m_fpsStats.frameCount = 0;
            m_fpsStats.startTime = nowTime;
        }
    }

private:
    double getCurrentTime()
    {
        using namespace std::chrono;
        auto ns = duration_cast<nanoseconds>(system_clock::now().time_since_epoch());
        return static_cast<double>(ns.count()) * 0.000001;
    }

    // FPS statistics
    struct {
        FpsClock::time_point startTime{FpsClock::now()};
        int64_t frameCount{0};
    } m_fpsStats;

    bool m_started = false;
    double m_startTime;
    std::vector<double> m_frameTimes;
};

#pragma once

#include <vector>
#include <string>
#include <cstddef>

#include "camera.hpp"
#include "urlLoader.hpp"


class CamerasSettings
{
private:
    std::vector<Camera> cameras_;
    size_t cameraCount_;
    std::vector<std::string> oldRtspPaths;

public:
    CamerasSettings() : cameraCount_(0) {}

    bool LoadCameras( std::string old_rtsp_path);
    void initCameras();
    std::vector<Camera> &getCameras();
    Camera getCamera(size_t index);
    size_t getCameraCount();
};

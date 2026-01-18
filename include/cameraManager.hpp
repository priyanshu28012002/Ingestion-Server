#pragma once

#include <vector>
#include <string>
#include <cstddef>

#include "camera.hpp"

class CamerasSettings
{
private:
    std::vector<Camera> cameras_;
    size_t cameraCount_;

public:
    CamerasSettings() : cameraCount_(0) {}

    bool LoadCameras(const std::string &old_rtsp_path);
    std::vector<Camera> &getCameras();
    Camera getCamera(size_t index);
    size_t getCameraCount();
};

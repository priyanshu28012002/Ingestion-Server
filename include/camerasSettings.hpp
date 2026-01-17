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

    bool LoadCameras(const std::string& old_rtsp_path);

    const std::vector<Camera>& getCameras() const;
    const Camera& getCamera(size_t id) const;
    size_t getCameraCount();
};

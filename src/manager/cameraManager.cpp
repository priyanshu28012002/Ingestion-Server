#include <cameraManager.hpp>
#include "urlLoader.hpp"


bool CamerasSettings::LoadCameras(const std::string &old_rtsp_path)
{


    return true;
}

size_t CamerasSettings::getCameraCount()
{
    return cameras_.size();
}
Camera CamerasSettings::getCamera(size_t index)
{
    return cameras_[index];
}

std::vector<Camera> &CamerasSettings::getCameras()
{
    return cameras_;
}

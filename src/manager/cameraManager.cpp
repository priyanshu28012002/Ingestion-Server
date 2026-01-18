#include <cameraManager.hpp>

bool CamerasSettings::LoadCameras(std::string old_rtsp_path)
{
    URLLoader loader(old_rtsp_path);
    loader.loadURLs();
    oldRtspPaths = loader.getURLs();
    return true;
}

void CamerasSettings::initCameras()
{
    int oldRtspPathCount = oldRtspPaths.size();

    std::cout << oldRtspPathCount << std::endl;
    for (int i = 0; i < oldRtspPathCount; i++)
    {
        cameras_.emplace_back(i,"Camera "+std::to_string(i),oldRtspPaths[i],"xyz",false);
    }
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

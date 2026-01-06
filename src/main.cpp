#include <memory>
#include <vector>

#include "camera.hpp"
#include "video_pipeline.hpp"
#include "pipeline_manager.hpp"
#include "logger.hpp"

int main(int argc, char *argv[])
{

    std::vector<Camera> cameras;
    cameras.reserve(4);
    LOG_INFO("Start the Server");

    cameras.emplace_back(
        0,
        "Camera_" + std::to_string(1),
        "rtsp://admin:qwerty123@192.168.1.3:554/Streaming/channels/101",
        "Mera Company");
    cameras.emplace_back(
        1,
        "Camera_" + std::to_string(2),
        "rtsp://admin:qwerty123@192.168.1.4:554/Streaming/channels/101",
        "Mera Company");
    cameras.emplace_back(
        2,
        "Camera_" + std::to_string(3),
        "rtsp://admin:qwerty123@192.168.1.23:554/Streaming/channels/101",
        "Mera Company");
    cameras.emplace_back(
        3,
        "Camera_" + std::to_string(4),
        "rtsp://admin:qwerty123@192.168.1.12:554/stream2",
        "Mera Company");

    LOG_INFO("Configer Camera"); 
    PipelineManager manager;

    for (const auto &cam : cameras)
    {
        manager.add_pipeline(
            std::make_unique<VideoPipeline>(cam));
    }

    LOG_INFO("Configer Pipeline");
    LOG_INFO("Started Video Stream");

    manager.run();
    
    LOG_INFO("Closed Video Stream");

    return 0;
}

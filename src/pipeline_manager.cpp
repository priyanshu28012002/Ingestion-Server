#include "pipeline_manager.hpp"
#include "video_pipeline.hpp"
#include "app_setting.hpp"

namespace PipelineConfig
{
    constexpr int RTSP_PORT = 8554;
    constexpr int TEST_STREAM_COUNT = 50;
    constexpr int WEBCAM_STREAM_COUNT = 50;
    constexpr int VIDEO_STREAM_COUNT = 50;
    // NVIDIA encoder settings for optimal performance
    constexpr int BITRATE = 400000;  // 4 Mbps
    constexpr int GOP_SIZE = 30;      // Keyframe interval
    constexpr const char* VIDEO_WIDTH = "1920";
    constexpr const char* VIDEO_HEIGHT = "1080";
    constexpr const char* FRAMERATE = "30/1";

}
enum class SourceType
{
    WEBCAMERA,
    FILE,
    RTSP,
    SCREENCAPTURE,
    TEST
};
PipelineManager::PipelineManager()
{
    // Initialize GStreamer library

    gst_init(nullptr, nullptr);
    // Create and run GLib main loop
    // This keeps the server running and handles all RTSP client connections
    loop_ = g_main_loop_new(nullptr, FALSE);
}

PipelineManager::~PipelineManager()
{
    stop();
}

void PipelineManager::add_pipeline(std::unique_ptr<VideoPipeline> pipeline)
{
    pipelines_.push_back(std::move(pipeline));
}

void PipelineManager::run()
{
    for (auto &pipeline : pipelines_)
    {
        pipeline->start(loop_);
    }
    // Block here until interrupted

    g_main_loop_run(loop_);
}

void PipelineManager::stop()
{
    for (auto &pipeline : pipelines_)
    {
        pipeline->stop();
    }
    if (loop_)
    {
        // Cleanup

        g_main_loop_quit(loop_);
        g_main_loop_unref(loop_);
        loop_ = nullptr;
    }
}

/*
    gst_deinit();

*/
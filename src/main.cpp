/**
 * @file main.cpp
 * @brief Entry Point of the server
 * @author Priyanshu Srivastava
 * @date 2024-01-15
 * @version 1.0
 */

#include <memory>
#include <vector>

#include "camera.hpp"
#include "video_pipeline.hpp"
#include "pipeline_manager.hpp"
#include "logger.hpp"
#include "camerasSettings.hpp"
#include <iostream>
#include <config.hpp>
#include <streamManager.hpp>

/**
 * @brief Main function to Entry Point
 * @param argc Number of command line arguments
 * @param argv Array of command line arguments
 * @return int Exit status (0 for success)
 */

/*

Algo Program Flow

First init Cameras
PipelineManager manager;




*/

int main(int argc, char *argv[])
{
    try
    {

        // 1. Initialize GStreamer (RAII - auto cleanup)
        // GstInitializer gst_init(&argc, &argv);

        std::unique_ptr<StreamManager> stream_manager_;
        std::unique_ptr<Config> config_;
        std::unique_ptr<CamerasSettings> cameras_settings_;
        std::unique_ptr<PipelineManager> pipeline_manager_;

        // 2. Setup logging
        Logger::get_instance()->set_log_level(Logger::Level::INFO);

        LOG_INFO("Starting RTSP Proxy System");

        // 3. Load configuration from file
        cameras_settings_->LoadCameras(config_->input_rtsp_path);
        // 4. Start RTSP server
       stream_manager_->InitStreamer(argc, argv);

        // 5. Initialize pipeline manager
        // 6. Add all streams (bulk operation)
        // 7. Run main loop (blocking)
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

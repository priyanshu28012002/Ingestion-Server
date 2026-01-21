#include "pipelineManager.hpp"
#include <iostream>

void PipelineManager::add_pipeline(GstRTSPMountPoints *mounts, GstRTSPMediaFactory *factory)
{
    if (!mounts || !factory)
    {
        g_printerr("Invalid mounts or factory\n");
        return;
    }

    const std::string input_url = "rtsp://localhost:8554/test0";
    const std::string mount_path = "/demo0";

    const std::string pipeline =
        "( rtspsrc location=" + input_url + " latency=0 protocols=tcp ! "
                                            "rtph264depay ! "
                                            "h264parse ! "
                                            "rtph264pay name=pay0 pt=96 )";

    gst_rtsp_media_factory_set_launch(factory, pipeline.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    gst_rtsp_mount_points_add_factory(
        mounts,
        mount_path.c_str(),
        factory);
}



void PipelineManager::register_all_camera_mounts(
    GstRTSPMountPoints* mounts,
    const std::vector<Camera> cams)
{
    if (!mounts)
    {
        g_printerr("Invalid mounts\n");
        return;
    }

    int count = 0;

    for (const auto& cam : cams)
    {
        GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
        if (!factory)
        {
            g_printerr("Failed to create media factory\n");
            continue;
        }

        const std::string input_url  = cam.sourceUri();
        const std::string mount_path = "/demo" + std::to_string(count);

        const std::string pipeline =
            "( rtspsrc location=" + input_url + " latency=0 protocols=tcp ! "
            "rtph264depay ! "
            "h264parse ! "
            "rtph264pay name=pay0 pt=96 )";

        gst_rtsp_media_factory_set_launch(factory, pipeline.c_str());
        gst_rtsp_media_factory_set_shared(factory, TRUE);
        gst_rtsp_media_factory_set_latency(factory, 200);


        gst_rtsp_mount_points_add_factory(
            mounts,
            mount_path.c_str(),
            factory
        );

        ++count;
    }
}



void PipelineManager::initPipeline()
{
    factory = gst_rtsp_media_factory_new();
    std::cout << "Pipeline Init" << std::endl;
}

#include <streamManager.hpp>
#include <iostream>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

StreamManager::StreamManager(/* args */)
{
}

void StreamManager::InitStreamer()
{
    initGstreamer();
    getRstpServer();
    setRtspServerPort(9000);
    getRtspMountPoints();
    // cameras_manager_->LoadCameras();
    // cameras_manager_->initCameras();
    pipeline_manager_->initPipeline();
    pipeline_manager_->add_pipeline(mounts);
    cleanUpMountPoints();
    attachServer();
    createGlibMainLoop();
    // runGLibMainLoop();

}

StreamManager::~StreamManager()
{
    stop();
}

void StreamManager::initGstreamer()
{
 gst_init(nullptr, nullptr);
}


GstRTSPServer *StreamManager::getRstpServer()
{
    if (server != nullptr)
    {
        std::cout<<"All ready Have the server Ptr"<<std::endl;
        return server;
    }

        server = gst_rtsp_server_new();

        if (!server)
        {
            g_printerr("Failed to create RTSP server\n");
            return nullptr;
        }
    
        std::cout<<"Created the server Ptr"<<std::endl;

    return server;
}

bool StreamManager::setRtspServerPort(int port)
{
    if (!server)
    {
        return false;
    }

    if (port <= 0 || port > 65535)
    {
        return false;
    }

    std::string portStr = std::to_string(port);
    gst_rtsp_server_set_service(server, portStr.c_str());

    return true;
}

GstRTSPMountPoints *StreamManager::getRtspMountPoints()
{
    if (mounts == nullptr)
    {
        mounts = gst_rtsp_server_get_mount_points(server);

        if (!mounts)
        {
            g_printerr("Failed to create RTSP server\n");
            return nullptr;
        }
    }

    return mounts;
}

bool StreamManager::addProxyStream(GstRTSPMountPoints *mounts,
                                   const std::string &input_url,
                                   const std::string &output_path)
{
    // Create media factory for this proxy stream
    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
    if (!factory)
    {
        std::cerr << "  ✗ Failed to create factory for " << output_path << std::endl;
        return false;
    }

    // Build pipeline for RTSP proxy (NO DECODING/ENCODING)
    // Pipeline breakdown:
    // - rtspsrc: Receives RTSP stream from source server
    //   - location: Source RTSP URL to connect to
    //   - latency=0: Minimize buffering for low latency
    //   - protocols=tcp: Use TCP for reliability (alternatively use udp for lower latency)
    // - rtph264depay: Extracts H.264 NAL units from RTP packets
    //   - This is a lightweight operation (just unpacking)
    // - rtph264pay: Repackages H.264 NAL units into RTP packets
    //   - name=pay0: Required name for RTSP server
    //   - pt=96: RTP payload type for H.264
    //   - This is also lightweight (just packing)
    //
    // KEY POINT: No videoconvert, no encoder, no decoder!
    // We're just moving H.264 data from one RTP stream to another
    std::string pipeline =
        "( rtspsrc location=" + input_url + " latency=0 protocols=tcp ! "
                                            "rtph264depay ! "
                                            "h264parse !"
                                            "rtph264pay name=pay0 pt=96 )";

    // Set the pipeline for this media factory
    gst_rtsp_media_factory_set_launch(factory, pipeline.c_str());

    // Enable shared mode - allows multiple clients to connect to same stream
    // Without this, only one client could connect at a time
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    // Mount the factory at the output path
    gst_rtsp_mount_points_add_factory(mounts, output_path.c_str(), factory);

    std::cout << "  ✓ Proxy: " << input_url << " → rtsp://localhost:9000" << output_path << std::endl;

    return true;
}

bool StreamManager::cleanUpMountPoints()
{
    g_object_unref(mounts);
}

bool StreamManager::attachServer()
{
    gst_rtsp_server_attach(server, NULL);
}

bool StreamManager::createGlibMainLoop()
{
    loop = g_main_loop_new(NULL, FALSE);
}
bool StreamManager::runGLibMainLoop()
{
    g_main_loop_run(loop);
}

void StreamManager::stop()
{
    if (server)
    {
        // gst_object_unref(server);
        g_object_unref(server);

        server = nullptr;
    }

    if (mounts)
    {
        // gst_object_unref(mounts);
        g_object_unref(mounts);

        mounts = nullptr;
    }

    if (loop)
    {
        // gst_object_unref(mounts);
        g_main_loop_unref(loop);

        mounts = nullptr;
    }

    gst_deinit();
}
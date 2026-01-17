#ifndef STREAM_MANAGER_HPP
#define STREAM_MANAGER_HPP

#include <string>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include "cameraManager.hpp"
#include "pipelineManager.hpp"



class StreamManager
{
private:
    GstRTSPServer *server = nullptr;
    GstRTSPMountPoints *mounts = nullptr;
    GMainLoop *loop = nullptr;
    
    std::unique_ptr<CamerasSettings> cameras_settings_;
    std::unique_ptr<PipelineManager> pipeline_manager_;

    void stop();

public:
    StreamManager(/* args */);
    ~StreamManager();
    void InitStreamer(int argc, char *argv[]);
    GstRTSPServer *getRstpServer();
    GstRTSPMountPoints *getRtspMountPoints();

    bool setRtspServerPort(int port);
    bool addProxyStream(GstRTSPMountPoints *mounts,
                        const std::string &input_url,
                        const std::string &output_path);
    bool cleanUpMountPoints();
    bool attachServer();

    bool createGlibMainLoop();
    bool runGLibMainLoop();

};

bool initCamera(std::string &OldRtspPath);

#endif // STREAM_MANAGER_HPP

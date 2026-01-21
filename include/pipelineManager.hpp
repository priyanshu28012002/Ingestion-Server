#pragma once

#include <gst/gst.h>
#include <memory>
#include <vector>
#include "pipeline.hpp"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
class PipelineManager {
public:

    
// void add_pipeline(std::unique_ptr<VideoPipeline> pipeline);
void add_pipeline(GstRTSPMountPoints *mounts,  GstRTSPMediaFactory * factory);
void register_all_camera_mounts(GstRTSPMountPoints *mounts , std::vector<Camera> cams);


void initPipeline();

private:
    GstRTSPMediaFactory* factory = nullptr;
    std::vector<std::unique_ptr<VideoPipeline>> pipelines_;
    void getRtspMediaFactory();
};

#include "pipelineManager.hpp"
#include <iostream>


void PipelineManager::add_pipeline(GstRTSPMountPoints *mounts){


}

void PipelineManager::initPipeline(){
    factory = gst_rtsp_media_factory_new();
    std::cout<<"Pipeline Init"<<std::endl;
}


// void PipelineManager::add_pipeline(std::unique_ptr<VideoPipeline> pipeline)
// {
//     pipelines_.push_back(std::move(pipeline));
// }

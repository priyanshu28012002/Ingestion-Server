#pragma once

#include <gst/gst.h>
#include <memory>
#include <vector>
#include "pipeline.hpp"


class PipelineManager {
public:

    
void add_pipeline(std::unique_ptr<VideoPipeline> pipeline);
// void add_pipeline(std::unique_ptr<VideoPipeline> pipeline);


private:

    std::vector<std::unique_ptr<VideoPipeline>> pipelines_;
};

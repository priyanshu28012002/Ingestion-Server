#include "pipelineManager.hpp"




void PipelineManager::add_pipeline(std::unique_ptr<VideoPipeline> pipeline)
{
    pipelines_.push_back(std::move(pipeline));
}

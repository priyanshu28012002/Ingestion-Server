#include "pipeline_manager.hpp"
#include "video_pipeline.hpp"

PipelineManager::PipelineManager() {
    gst_init(nullptr, nullptr);
    loop_ = g_main_loop_new(nullptr, FALSE);
}

PipelineManager::~PipelineManager() {
    stop();
}

void PipelineManager::add_pipeline(std::unique_ptr<VideoPipeline> pipeline) {
    pipelines_.push_back(std::move(pipeline));
}

void PipelineManager::run() {
    for (auto& pipeline : pipelines_) {
        pipeline->start(loop_);
    }
    g_main_loop_run(loop_);
}

void PipelineManager::stop() {
    for (auto& pipeline : pipelines_) {
        pipeline->stop();
    }
    if (loop_) {
        g_main_loop_quit(loop_);
        g_main_loop_unref(loop_);
        loop_ = nullptr;
    }
}

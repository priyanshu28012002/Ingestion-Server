#pragma once

#include <gst/gst.h>
#include <memory>
#include <vector>

class VideoPipeline;

class PipelineManager {
public:
    PipelineManager();
    ~PipelineManager();

    void add_pipeline(std::unique_ptr<VideoPipeline> pipeline);
    void run();
    void stop();

private:
    GMainLoop* loop_{nullptr};
    std::vector<std::unique_ptr<VideoPipeline>> pipelines_;
};

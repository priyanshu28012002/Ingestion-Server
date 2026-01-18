/**
 * @file video_pipeline.cpp
 * @brief Make the Pipeline For the each stream
 * @author Priyanshu Srivastava
 * @date 2025-01-15
 * @version 1.3.0.3
 */

#ifndef GET_PIPELINE_HPP
#define GET_PIPELINE_HPP

#include <gst/gst.h>
#include "camera.hpp"

/**
 * @class VideoPipeline
 * @brief Main video processing and streaming pipeline
 *
 * Handles video input, processing, encoding, and streaming
 * through multiple protocols (RTSP, HTTP, WebRTC).
 */
class VideoPipeline
{
public:
    /**
     * @brief Construct a new VideoPipeline object
     * @param config_path Path to configuration file
     */
    explicit VideoPipeline(const Camera &camera);
    ~VideoPipeline();

    /**
     * @brief Start video streaming
     * @param stream_name Name of the stream
     * @return Stream ID on success, -1 on failure
     */
    
    bool start();
    void stop();
    bool init_video_pipeline();
    bool start(GMainLoop *loop);

private:
    bool create_elements();
    bool link_elements();
    static void on_pad_added(GstElement *src, GstPad *pad, gpointer data);

private:
    const Camera &camera_;

    GstElement *pipeline_{nullptr};
    GstElement *source_{nullptr};
    GstElement *depay_{nullptr};
    GstElement *parse_{nullptr};
    GstElement *decode_{nullptr};
    GstElement *sink_{nullptr};

    GstBus *bus_{nullptr};
    GMainLoop *loop_{nullptr};
};

#endif // GET_PIPELINE_HPP
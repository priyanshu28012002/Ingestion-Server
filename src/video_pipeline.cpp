/**
 * @file video_pipeline.cpp
 * @brief Implementation of VideoPipeline class
 * @author Priyanshu Srivastava
 * @date 2025-01-15
 * @version 1.3.0.3
 */


#include "video_pipeline.hpp"
#include <iostream>
#include <gst/rtsp/gstrtsptransport.h>



/**
 * @brief VideoPipeline constructor implementation
 * @details Loads configuration from JSON file and initializes
 * internal components.
 * 
 * @note Configuration file must contain camera settings and
 * streaming parameters.
 */
VideoPipeline::VideoPipeline(const Camera &camera)
    : camera_(camera) {}

VideoPipeline::~VideoPipeline()
{
    stop();
}

bool VideoPipeline::create_elements()
{
    source_ = gst_element_factory_make("rtspsrc", nullptr);
    depay_ = gst_element_factory_make("rtph265depay", nullptr);
    parse_ = gst_element_factory_make("h265parse", nullptr);
    decode_ = gst_element_factory_make("avdec_h265", nullptr);
    sink_ = gst_element_factory_make("xvimagesink", nullptr);

    pipeline_ = gst_pipeline_new(nullptr);

    if (!pipeline_ || !source_ || !depay_ || !parse_ || !decode_ || !sink_)
    {
        std::cerr << "Failed to create GStreamer elements\n";
        return false;
    }

    gst_bin_add_many(
        GST_BIN(pipeline_),
        source_, depay_, parse_, decode_, sink_, nullptr);

    return true;
}

bool VideoPipeline::link_elements()
{
    if (!gst_element_link_many(depay_, parse_, decode_, sink_, nullptr))
    {
        std::cerr << "Failed to link elements\n";
        return false;
    }

    g_signal_connect(
        source_, "pad-added",
        G_CALLBACK(VideoPipeline::on_pad_added),
        depay_);

    return true;
}

void VideoPipeline::on_pad_added(GstElement *src, GstPad *pad, gpointer data)
{
    GstElement *depay = static_cast<GstElement *>(data);
    GstPad *sink_pad = gst_element_get_static_pad(depay, "sink");

    if (!gst_pad_is_linked(sink_pad))
    {
        gst_pad_link(pad, sink_pad);
    }

    gst_object_unref(sink_pad);
}

/**
 * @brief Start a video stream
 * @details Creates encoder, muxer, and server components for
 * the specified stream.
 * 
 * @param stream_name Unique identifier for the stream
 * @return Stream ID that can be used to control the stream
 * 
 * @throw std::invalid_argument if stream_name is empty
 * @throw std::runtime_error if stream cannot be created
 * 
 * @code
 * VideoPipeline pipeline("config.json");
 * int stream_id = pipeline.start_stream("camera1");
 * if (stream_id >= 0) {
 *     // Stream started successfully
 * }
 * @endcode
 */

 
bool VideoPipeline::start()
{
    if (!create_elements() || !link_elements())
        return false;

    g_object_set(
        source_,
        "location", camera_.sourceUri().c_str(),
        "latency", 500,
        "protocols", GST_RTSP_LOWER_TRANS_UDP,
        nullptr);

    loop_ = g_main_loop_new(nullptr, FALSE);
    bus_ = gst_element_get_bus(pipeline_);

    gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    std::cout << "Started pipeline for camera: "
              << camera_.name() << "\n";

    return true;
}

void VideoPipeline::stop()
{
    if (!pipeline_)
        return;

    gst_element_set_state(pipeline_, GST_STATE_NULL);

    if (bus_)
        gst_object_unref(bus_);
    if (loop_)
        g_main_loop_unref(loop_);
    gst_object_unref(pipeline_);

    pipeline_ = nullptr;
}

bool VideoPipeline::start(GMainLoop* loop) {
    if (!create_elements() || !link_elements())
        return false;

    loop_ = loop;

    bus_ = gst_element_get_bus(pipeline_);
    gst_bus_add_watch(bus_, nullptr, nullptr); // temporary

    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    return true;
}
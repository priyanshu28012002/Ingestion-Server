#ifndef GET_PIPELINE_HPP
#define GET_PIPELINE_HPP

#include <gst/gst.h>
#include "camera.hpp"

class VideoPipeline {
public:
    explicit VideoPipeline(const Camera& camera);
    ~VideoPipeline();

    bool start();
    void stop();
    bool init_video_pipeline();
    bool start(GMainLoop* loop); 

private:
    bool create_elements();
    bool link_elements();
    static void on_pad_added(GstElement* src, GstPad* pad, gpointer data);

private:
    const Camera& camera_;

    GstElement* pipeline_{nullptr};
    GstElement* source_{nullptr};
    GstElement* depay_{nullptr};
    GstElement* parse_{nullptr};
    GstElement* decode_{nullptr};
    GstElement* sink_{nullptr};

    GstBus* bus_{nullptr};
    GMainLoop* loop_{nullptr};
};

#endif // GET_PIPELINE_HPP
#pragma once
#include <string>

struct AppSettings {

    /* ---------- RTSP SERVER ---------- */
    int rtsp_port = 8554;
    std::string rtsp_mount_prefix = "/cam";

    /* ---------- INPUT ---------- */
    int rtsp_latency_ms = 200;
    bool force_tcp = false;

    /* ---------- PROCESSING ---------- */
    bool enable_processing = true;   // Option B switch
    bool draw_overlay = false;       // future use

    /* ---------- ENCODING ---------- */
    std::string output_codec = "h264";   // "h264" | "h265"
    int video_bitrate_kbps = 2000;
    bool low_latency = true;

    /* ---------- DEBUG ---------- */
    bool verbose = true;

    
};

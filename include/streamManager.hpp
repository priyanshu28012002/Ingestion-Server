#ifndef STREAM_MANAGER_HPP
#define STREAM_MANAGER_HPP

#include <string>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>



class StreamManager
{
private:
    GstRTSPServer *server = nullptr;
    GstRTSPMountPoints *mounts = nullptr;

    void stop();

public:
    StreamManager(/* args */);
    ~StreamManager();
    void InitStreamer(int argc, char *argv[]);
    GstRTSPServer *getRstpServer();
    GstRTSPMountPoints *getRtspMountPoints();

    bool setRtspServerPort(std::string port);
};

bool initCamera(std::string &OldRtspPath);

#endif // STREAM_MANAGER_HPP

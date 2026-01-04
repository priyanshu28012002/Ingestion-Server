#include <gst/gst.h>              
#include <gst/rtsp-server/rtsp-server.h>
#include <iostream>
#include <string>

#define DEFAULT_RTSP_PORT "8554"   
#define DEFAULT_MOUNT_POINT "/webcam"

int main(int argc, char *argv[]) {
    
    /*************************************************************
     * STEP 1: Initialize GStreamer
     * 
     * This must be done before any other GStreamer operations.
     * It sets up the internal structures and loads plugins.
     *************************************************************/
    gst_init(&argc, &argv);
    
    /*************************************************************
     * STEP 2: Create the main event loop
     * 
     * This loop keeps the program running and handles events.
     * Without this, the program would exit immediately.
     *************************************************************/
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    
    /*************************************************************
     * STEP 3: Create the RTSP server
     * 
     * This creates a new RTSP server instance that will handle
     * client connections and stream management.
     *************************************************************/
    GstRTSPServer *server = gst_rtsp_server_new();
    
    /*************************************************************
     * STEP 4: Configure server port
     * 
     * Check if a custom port is specified in environment variable,
     * otherwise use the default port (8554).
     *************************************************************/
    const gchar *port = g_getenv("GST_RTSP_PORT");  // Check environment variable
    if (!port) port = DEFAULT_RTSP_PORT;           // Use default if not set
    
    // Set the port the server will listen on
    gst_rtsp_server_set_service(server, port);
    
    // Print server information to console
    std::cout << "===============================================" << std::endl;
    std::cout << "RTSP Video Streaming Server" << std::endl;
    std::cout << "===============================================" << std::endl;
    std::cout << "Server listening on port: " << port << std::endl;
    
    /*************************************************************
     * STEP 5: Set up mount points (URL paths)
     * 
     * Mount points define the URLs where streams are available.
     * Example: rtsp://localhost:8554/webcam
     *************************************************************/
    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);
    
    /*************************************************************
     * STEP 6: Create a media factory
     * 
     * The media factory defines how to create the media stream.
     * It contains the GStreamer pipeline that captures and encodes video.
     *************************************************************/
    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
    
    /*************************************************************
     * STEP 7: Define the GStreamer pipeline
     * 
     * This pipeline describes how to process video:
     * 1. Capture from webcam
     * 2. Convert to proper format
     * 3. Scale to desired size
     * 4. Encode to H.264 (compressed format)
     * 5. Package for network streaming
     *************************************************************/
    const char *pipeline_description = 
        "( "  // Start of pipeline
        
        // Source element: Capture from webcam (v4l2 = Video for Linux 2)
        "v4l2src device=/dev/video0 ! "
        
        // Converter: Convert between different color formats if needed
        "videoconvert ! "
        
        // Scaler: Resize the video to 640x480 pixels
        "videoscale ! "
        
        // Video format specification:
        // - width=640, height=480: Set resolution
        // - framerate=30/1: Set frame rate to 30 frames per second
        // - format=NV12: Force YUV 4:2:0 format (compatible with H.264)
        "video/x-raw,width=640,height=480,framerate=30/1,format=NV12 ! "
        
        // H.264 encoder: Compress video for streaming
        // - speed-preset=ultrafast: Fast encoding (lower quality but less delay)
        // - tune=zerolatency: Minimize delay for real-time streaming
        // - bitrate=500: Set bitrate to 500 kbps
        "x264enc speed-preset=ultrafast tune=zerolatency bitrate=500 ! "
        
        // RTP payloader: Package H.264 video into RTP packets for network transmission
        // - name=pay0: Name this element for RTSP control
        // - pt=96: Payload type 96 (standard for H.264)
        "rtph264pay name=pay0 pt=96 "
        
        ")";  // End of pipeline
    
    /*************************************************************
     * STEP 8: Configure the media factory with our pipeline
     *************************************************************/
    gst_rtsp_media_factory_set_launch(factory, pipeline_description);
    
    // Set shared mode: Multiple clients can connect to the same stream
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    
    /*************************************************************
     * STEP 9: Add the factory to mount points
     * 
     * This makes the stream available at the specified URL path.
     *************************************************************/
    gst_rtsp_mount_points_add_factory(mounts, DEFAULT_MOUNT_POINT, factory);
    
    // We don't need the mounts reference anymore, release it
    g_object_unref(mounts);
    
    /*************************************************************
     * STEP 10: Display connection information
     *************************************************************/
    std::cout << "Stream URL: rtsp://localhost:" << port << DEFAULT_MOUNT_POINT << std::endl;
    std::cout << "===============================================" << std::endl;
    
    // For remote access, also show the IP address
    std::cout << "For remote access, use your computer's IP address:" << std::endl;
    std::cout << "Example: rtsp://192.168.1.100:" << port << DEFAULT_MOUNT_POINT << std::endl;
    std::cout << "===============================================" << std::endl;
    
    /*************************************************************
     * STEP 11: Attach server to main context
     * 
     * This connects the server to the main event loop so it can
     * handle incoming client connections.
     *************************************************************/
    if (!gst_rtsp_server_attach(server, NULL)) {
        std::cerr << "ERROR: Failed to attach server to main context!" << std::endl;
        std::cerr << "Make sure port " << port << " is not already in use." << std::endl;
        return -1;  // Exit with error code
    }
    
    /*************************************************************
     * STEP 12: Start the server
     * 
     * The main loop will run until interrupted (Ctrl+C).
     * While running, it handles:
     * - New client connections
     * - Video streaming
     * - Client disconnections
     *************************************************************/
    std::cout << "Server started successfully!" << std::endl;
    std::cout << "Press Ctrl+C to stop the server." << std::endl;
    std::cout << "===============================================" << std::endl;
    
    // Start the main loop - this blocks until program is stopped
    g_main_loop_run(loop);
    
    /*************************************************************
     * STEP 13: Cleanup (when Ctrl+C is pressed)
     * 
     * Release all resources when the program ends.
     *************************************************************/
    g_main_loop_unref(loop);  // Release the main loop
    g_object_unref(server);   // Release the server
    
    std::cout << "Server stopped. Goodbye!" << std::endl;
    
    return 0;  // Exit successfully
}
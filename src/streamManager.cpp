// /**
//  * @class StreamManager
//  * @brief Manages the creation and configuration of multiple RTSP streams
//  * 
//  * This class handles the bulk creation of test, webcam, and file streams,
//  * distributing the load efficiently across the system.
//  */
// class StreamManager {
// public:
//     /**
//      * @brief Initializes all configured streams
//      * @param video_file Path to video file for file-based streams
//      * @param webcam_device Path to webcam device
//      * @return true if all streams initialized successfully
//      */
//     bool initializeStreams(const std::string& video_file = Config::DEFAULT_VIDEO_FILE,
//                           const std::string& webcam_device = Config::DEFAULT_WEBCAM_DEVICE) {
//         std::cout << "\n=== Initializing RTSP Streams ===" << std::endl;
        
//         // Create test pattern streams
//         std::cout << "\n[1/3] Creating " << Config::TEST_STREAM_COUNT << " test streams..." << std::endl;
//         if (!createTestStreams()) {
//             std::cerr << "Failed to create test streams" << std::endl;
//             return false;
//         }
        
//         // Create webcam streams
//         std::cout << "\n[2/3] Creating " << Config::WEBCAM_STREAM_COUNT << " webcam streams..." << std::endl;
//         if (!createWebcamStreams(webcam_device)) {
//             std::cerr << "Failed to create webcam streams" << std::endl;
//             return false;
//         }
        
//         // Create file-based streams
//         std::cout << "\n[3/3] Creating " << Config::VIDEO_STREAM_COUNT << " video file streams..." << std::endl;
//         if (!createFileStreams(video_file)) {
//             std::cerr << "Failed to create file streams" << std::endl;
//             return false;
//         }
        
//         std::cout << "\n✓ All streams initialized successfully!" << std::endl;
//         std::cout << "Total streams: " << stream_configs_.size() << std::endl;
        
//         return true;
//     }
    
//     /**
//      * @brief Gets the list of all configured streams
//      * @return Vector of stream configurations
//      */
//     const std::vector<StreamConfig>& getStreams() const {
//         return stream_configs_;
//     }
    
//     /**
//      * @brief Prints a summary of all available streams
//      */
//     void printStreamSummary() const {
//         std::cout << "\n=== Available RTSP Streams ===" << std::endl;
//         std::cout << "Connect using: rtsp://localhost:" << Config::RTSP_PORT << "/<stream_path>\n" << std::endl;
        
//         std::cout << "Test Streams (0-" << Config::TEST_STREAM_COUNT - 1 << "):" << std::endl;
//         std::cout << "  rtsp://localhost:" << Config::RTSP_PORT << "/test[0-" << Config::TEST_STREAM_COUNT - 1 << "]" << std::endl;
        
//         std::cout << "\nWebcam Streams (0-" << Config::WEBCAM_STREAM_COUNT - 1 << "):" << std::endl;
//         std::cout << "  rtsp://localhost:" << Config::RTSP_PORT << "/webcam[0-" << Config::WEBCAM_STREAM_COUNT - 1 << "]" << std::endl;
        
//         std::cout << "\nVideo File Streams (0-" << Config::VIDEO_STREAM_COUNT - 1 << "):" << std::endl;
//         std::cout << "  rtsp://localhost:" << Config::RTSP_PORT << "/video[0-" << Config::VIDEO_STREAM_COUNT - 1 << "]" << std::endl;
        
//         std::cout << "\n==================================" << std::endl;
//     }

// private:
//     std::vector<StreamConfig> stream_configs_;
    
//     /**
//      * @brief Creates test pattern streams
//      * Each stream uses a different test pattern variant
//      */
//     bool createTestStreams() {
//         for (int i = 0; i < Config::TEST_STREAM_COUNT; ++i) {
//             std::string mount_point = "/test" + std::to_string(i);
//             std::string pipeline = PipelineFactory::createTestPipeline(i);
            
//             stream_configs_.emplace_back(mount_point, pipeline, SourceType::TEST);
            
//             if (!RtspServer::instance().addStream(stream_configs_.back())) {
//                 return false;
//             }
//         }
//         return true;
//     }
    
//     /**
//      * @brief Creates webcam streams (all sharing the same camera source)
//      * Uses shared mode to allow multiple RTSP endpoints from single webcam
//      */
//     bool createWebcamStreams(const std::string& device) {
//         std::string pipeline = PipelineFactory::createWebcamPipeline(device);
        
//         for (int i = 0; i < Config::WEBCAM_STREAM_COUNT; ++i) {
//             std::string mount_point = "/webcam" + std::to_string(i);
            
//             stream_configs_.emplace_back(mount_point, pipeline, SourceType::WEBCAMERA);
            
//             if (!RtspServer::instance().addStream(stream_configs_.back())) {
//                 return false;
//             }
//         }
//         return true;
//     }
    
//     /**
//      * @brief Creates file-based streams (all sharing the same video file)
//      * Uses shared mode to allow multiple RTSP endpoints from single file
//      */
//     bool createFileStreams(const std::string& file_path) {
//         std::string pipeline = PipelineFactory::createFilePipeline(file_path);
        
//         for (int i = 0; i < Config::VIDEO_STREAM_COUNT; ++i) {
//             std::string mount_point = "/video" + std::to_string(i);
            
//             stream_configs_.emplace_back(mount_point, pipeline, SourceType::FILE);
            
//             if (!RtspServer::instance().addStream(stream_configs_.back())) {
//                 return false;
//             }
//         }
//         return true;
//     }
// };

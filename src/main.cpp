/**
 * @file main.cpp
 * @brief Entry Point of the server
 * @author Priyanshu Srivastava
 * @date 2024-01-15
 * @version 1.0
 */

#include <memory>
#include <vector>
#include "logger.hpp"
#include <iostream>
#include <config.hpp>
#include <streamManager.hpp>

/**
 * @brief Main function to Entry Point
 * @param argc Number of command line arguments
 * @param argv Array of command line arguments
 * @return int Exit status (0 for success)
 */


int main(int argc, char *argv[])
{
    try
    {

        std::unique_ptr<StreamManager> stream_manager_;
        std::unique_ptr<Config> config_;
        Logger::get_instance()->set_log_level(Logger::Level::INFO);

        LOG_INFO("Starting RTSP Proxy System");

        stream_manager_->InitStreamer(argc, argv);
        stream_manager_->getRstpServer();
        stream_manager_->setRtspServerPort(9000);
        stream_manager_->getRtspMountPoints();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}


/*

Lifeline Management 
Signal Handeling

*/
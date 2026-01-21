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
#include <fileio.hpp>
#include <cameraManager.hpp>

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
        stream_manager_ = std::make_unique<StreamManager>();
        stream_manager_->InitStreamer();
        
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
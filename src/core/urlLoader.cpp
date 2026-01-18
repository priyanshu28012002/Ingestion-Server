#include <iostream>
#include <string>
#include <vector>
#include "fileio.hpp"
#include "urlLoader.hpp"

URLLoader::URLLoader(const std::string &filename) : fileIO(nullptr)
{
    try
    {
        fileIO = new FileIO(filename);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error initializing FileIO: " << e.what() << std::endl;
        throw;
    }
}

URLLoader::~URLLoader()
{
    if (fileIO)
    {
        delete fileIO;
    }
}

// Parse the memory-mapped file and extract URLs
void URLLoader::loadURLs()
{
    if (!fileIO || !fileIO->getMappedMemory())
    {
        throw std::runtime_error("File not properly mapped");
    }

    const char *data = static_cast<const char *>(fileIO->getMappedMemory());
    size_t fileSize = fileIO->getFileSize();

    std::string currentURL;

    for (size_t i = 0; i < fileSize; ++i)
    {
        char c = data[i];

        // Check for newline characters (both \n and \r\n)
        if (c == '\n' || c == '\r')
        {
            // If we have accumulated a URL, add it to the vector
            if (!currentURL.empty())
            {
                // Trim any trailing whitespace
                size_t end = currentURL.find_last_not_of(" \t\r\n");
                if (end != std::string::npos)
                {
                    currentURL = currentURL.substr(0, end + 1);
                }

                urls.push_back(currentURL);
                currentURL.clear();
            }
        }
        else
        {
            currentURL += c;
        }
    }

    // Don't forget the last URL if file doesn't end with newline
    if (!currentURL.empty())
    {
        size_t end = currentURL.find_last_not_of(" \t\r\n");
        if (end != std::string::npos)
        {
            currentURL = currentURL.substr(0, end + 1);
        }
        urls.push_back(currentURL);
    }
}

// Get the loaded URLs
const std::vector<std::string> &URLLoader::getURLs() const
{
    return urls;
}

// Print all loaded URLs
void URLLoader::printURLs() const
{
    std::cout << "Loaded " << urls.size() << " URLs:" << std::endl;
    for (size_t i = 0; i < urls.size(); ++i)
    {
        std::cout << i + 1 << ". " << urls[i] << std::endl;
    }
}

/*

int main() {
    try {
        // Create URLLoader with your text file
        URLLoader loader("/home/octo/Desktop/motion_progress/Ingestion-Server/config/company_rtsp_path.txt");

        // Load URLs from the memory-mapped file
        loader.loadURLs();

        // Print all URLs
        loader.printURLs();

        // Access individual URLs
        const std::vector<std::string>& urls = loader.getURLs();

        std::cout << "\nProcessing URLs:" << std::endl;
        for (const auto& url : urls) {
            std::cout << "Processing: " << url << std::endl;
            // Do something with each URL here
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

*/
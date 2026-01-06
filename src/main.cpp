#include <logger.hpp>
#include <iostream>
#include <vector>
#include "camera.hpp"

int main() {
    int cameraCount = 5;

    std::vector<Camera> cameras;
    cameras.reserve(cameraCount);  


    //Read the Json File and get the meta data of the camera and make the camera instance
    for (int i = 0; i < cameraCount; ++i) {
        cameras.emplace_back(
            i,
            "Camera_" + std::to_string(i),
            "rtsp://192.168.1." + std::to_string(100 + i) + "/stream" , // replace the URL 
            "Mera Comapny"
        );
    }

    
    std::cout << "Created " << cameras.size()
              << " camera objects\n";

    return 0;
}

#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <string>

// camera.hpp
/**
 * @class Camera
 * @brief Represents a camera device for video capture
 *
 * This class handles camera initialization, frame capture,
 * and stream management.
 */

enum class SourceType
{
    WEBCAMERA,
    FILE,
    RTSP,
    SCREENCAPTURE,
    TEST
};


class Camera
{
public:
    /**
     * @brief Construct a new Camera object
     * @param camera_id Camera device ID (0 for default)
     * @param width Frame width
     * @param height Frame height
     * @throw std::runtime_error if camera cannot be opened
     */
    Camera(int id,
           const std::string &name,
           const std::string &sourceUri,
           const std::string &companyName,
           const bool &isMicrophone);

    int id() const noexcept;
    const std::string &name() const noexcept;
    const std::string &sourceUri() const noexcept;
    const std::string &companyName() const noexcept;
    const bool &isMicrophone() const noexcept;
    std::string getProxyUrl();

private:
    int id_;
    std::string name_;
    std::string sourceUri_;
    std::string companyName_;
    std::string proxyUrl_;
    bool isMicrophone_;
};

#endif // CAMERA_HPP

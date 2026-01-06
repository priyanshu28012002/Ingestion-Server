#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <string>

class Camera {
public:
    Camera(int id,
           const std::string& name,
           const std::string& sourceUri,
           const std::string& companyName);

    int id() const noexcept;
    const std::string& name() const noexcept;
    const std::string& sourceUri() const noexcept;
    const std::string& companyName() const noexcept;

private:
    int id_;
    std::string name_;
    std::string sourceUri_;
    std::string companyName_;
};

#endif // CAMERA_HPP

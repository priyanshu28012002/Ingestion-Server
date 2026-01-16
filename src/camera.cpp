#include "camera.hpp"

Camera::Camera(int id,
               const std::string &name,
               const std::string &sourceUri,
               const std::string &companyName,
               const bool &isMicrophone)
    : id_(id),
      name_(name),
      sourceUri_(sourceUri),
      companyName_(companyName),
      isMicrophone_(isMicrophone) {}

int Camera::id() const noexcept
{
    return id_;
}

const std::string &Camera::name() const noexcept
{
    return name_;
}

const std::string &Camera::sourceUri() const noexcept
{
    return sourceUri_;
}

const std::string &Camera::companyName() const noexcept
{
    return companyName_;
}

const bool &Camera::isMicrophone() const noexcept
{
    return isMicrophone_;
}


std::string Camera::getProxyUrl(){
    return proxyUrl_;
}

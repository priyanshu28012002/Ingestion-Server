cmake_minimum_required(VERSION 3.16)
project(RTSPStreamingProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)

pkg_check_modules(GSTREAMER REQUIRED
    gstreamer-1.0
    gstreamer-rtsp-server-1.0
)

pkg_check_modules(GSTREAMER_VIDEO REQUIRED
    gstreamer-video-1.0
)
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
pkg_check_modules(SOUP REQUIRED
    libsoup-3.0
)

include_directories(
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_VIDEO_INCLUDE_DIRS}
    ${SOUP_INCLUDE_DIRS}
)
add_executable(control_server
    src/core/logger.cpp
    src/core/camera.cpp
    src/core/pipeline.cpp

    src/manager/pipelineManager.cpp
    src/manager/cameraManager.cpp
    src/manager/streamManager.cpp

    src/main.cpp

    config/config.hpp

)

target_link_libraries(control_server
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
)

target_include_directories(control_server
    PRIVATE
        ${PROJECT_NAME}/include
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTREAMER_VIDEO_INCLUDE_DIRS}
        ${SOUP_INCLUDE_DIRS}
)

include_directories(
  include
  config

)

target_link_libraries(control_server
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_VIDEO_LIBRARIES}
    ${SOUP_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
)


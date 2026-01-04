cmake_minimum_required(VERSION 3.16)
project(ingestion_server VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# Include directories
include_directories(include)

# Find GStreamer packages (including app sink support)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-video-1.0
    gstreamer-app-1.0
)
# Include directories for GStreamer
include_directories(
    ${GST_INCLUDE_DIRS}
)

link_directories(${GST_LIBRARY_DIRS})

# Source files
set(SOURCES
    src/server.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link with GStreamer libraries
target_link_libraries(${PROJECT_NAME} 
    ${GST_LIBRARIES}
)

# Pass pkg-config flags to the compiler and linker
target_compile_options(${PROJECT_NAME}  PRIVATE ${GST_CFLAGS_OTHER})
target_link_options(${PROJECT_NAME}  PRIVATE ${GST_LDFLAGS_OTHER})

# Print some debug info during CMake generation
message(STATUS "GStreamer include dirs: ${GST_INCLUDE_DIRS}")
message(STATUS "GStreamer libraries: ${GST_LIBRARIES}")

# Set properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Installation instructions
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Print configuration summary
message(STATUS "======================================")
message(STATUS " Ingestion Server Configuration ")
message(STATUS "======================================")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Source files: ${SOURCES}")
message(STATUS "Build directory: ${CMAKE_BINARY_DIR}")
message(STATUS "======================================")

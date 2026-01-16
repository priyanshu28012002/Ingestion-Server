# Ingestion-Server



## Overview

This RTSP (Real Time Streaming Protocol) server streams live video from your webcam over the network. Clients can connect using any media player that supports RTSP.

**Basic Flow:**
```
Webcam → Capture → Convert → Scale → Encode → Package → Network → Client
```

## Installation

### Prerequisites Installation

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential
sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libgstrtspserver-1.0-dev gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav gstreamer1.0-tools
```

### Compilation

```bash
# Basic compilation
g++ -o rtsp_server rtsp_server.cpp \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0) \
    -std=c++11

# Compilation with debug symbols
g++ -o rtsp_server rtsp_server.cpp \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0) \
    -std=c++11 -g -Wall

# Advanced compilation with optimization
g++ -o rtsp_server rtsp_server.cpp \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-rtsp-server-1.0) \
    -std=c++11 -O2 -march=native
```

## Pipeline Configuration

### Basic Pipeline Template
```bash
# Template with placeholders
"( v4l2src device=DEVICE ! "
"videoconvert ! "
"videoscale ! "
"video/x-raw,width=WIDTH,height=HEIGHT,framerate=FPS/1,format=FORMAT ! "
"x264enc speed-preset=PRESET tune=TUNE bitrate=BITRATE ! "
"rtph264pay name=pay0 pt=96 )"
```

### Pipeline Elements Deep Dive

#### 1. **v4l2src** (Video Source)
```cpp
// Basic
"v4l2src device=/dev/video0 ! "

// With additional controls
"v4l2src device=/dev/video0 do-timestamp=true ! "
```

**Common devices:**
- `/dev/video0` - First webcam
- `/dev/video1` - Second webcam
- `/dev/video2` - Virtual camera, etc.

#### 2. **videoconvert** (Format Converter)
```cpp
"videoconvert ! "  // Automatically converts between color spaces
```

#### 3. **videoscale** (Resolution Control)
```cpp
"videoscale ! "  // Resizes frames to specified dimensions
```

#### 4. **Video Format Specification (Caps Filter)**
```cpp
// Format: video/x-raw
// Parameters can be adjusted:
"video/x-raw,"
"width=640,"        // Horizontal resolution
"height=480,"       // Vertical resolution  
"framerate=30/1,"   // Frames per second (numerator/denominator)
"format=NV12"       // Color format (I420, NV12, YUY2, etc.)
" ! "
```

**Supported Formats:**
- `NV12` or `I420` - YUV 4:2:0 (recommended for H.264)
- `YUY2` - YUV 4:2:2 (common webcam output)
- `RGB` or `BGR` - Raw color formats

#### 5. **x264enc** (H.264 Encoder)
```cpp
"x264enc "
"speed-preset=ultrafast "  // Encoding speed/quality trade-off
"tune=zerolatency "        // Optimize for real-time streaming
"bitrate=500 "             // Target bitrate in kbps
" ! "
```

#### 6. **rtph264pay** (Network Packager)
```cpp
"rtph264pay "
"name=pay0 "      // Element name (required for RTSP)
"pt=96 "          // Payload type (96 = dynamic H.264)
"config-interval=1 "  // Send config packets interval
" ! "
```

## Stream Tuning Guide

### 1. **Quality vs Latency Trade-offs**

#### **Ultra-Low Latency** (Video calls, gaming)
```cpp
"x264enc speed-preset=ultrafast tune=zerolatency bitrate=1000 key-int-max=15 ! "
```
- **Latency**: ~50-100ms
- **Use Case**: Real-time communication

#### **Balanced** (General streaming)
```cpp
"x264enc speed-preset=medium tune=zerolatency bitrate=1500 key-int-max=30 ! "
```
- **Latency**: ~150-300ms
- **Use Case**: Surveillance, live events

#### **High Quality** (Recording, broadcasting)
```cpp
"x264enc speed-preset=slow tune=film bitrate=2500 key-int-max=60 ! "
```
- **Latency**: ~500-1000ms
- **Use Case**: Video production

### 2. **Resolution Presets**

#### **Low Bandwidth** (Mobile networks)
```cpp
"video/x-raw,width=320,height=240,framerate=15/1,format=NV12 ! "
"x264enc bitrate=250 ! "
```
- **Bandwidth**: ~300 kbps
- **CPU Usage**: Low

#### **Standard Definition** (General use)
```cpp
"video/x-raw,width=640,height=480,framerate=30/1,format=NV12 ! "
"x264enc bitrate=500 ! "
```
- **Bandwidth**: ~600 kbps
- **CPU Usage**: Medium

#### **High Definition** (Good networks)
```cpp
"video/x-raw,width=1280,height=720,framerate=30/1,format=NV12 ! "
"x264enc bitrate=1500 ! "
```
- **Bandwidth**: ~1.8 Mbps
- **CPU Usage**: High

#### **Full HD** (Local network)
```cpp
"video/x-raw,width=1920,height=1080,framerate=30/1,format=NV12 ! "
"x264enc bitrate=3000 ! "
```
- **Bandwidth**: ~3.5 Mbps
- **CPU Usage**: Very High

### 3. **Frame Rate Settings**

```cpp
// Low motion (talking head)
"framerate=15/1"  // 15 FPS - Low CPU, good for static scenes

// General purpose  
"framerate=30/1"  // 30 FPS - Smooth for most applications

// High motion (sports, games)
"framerate=60/1"  // 60 FPS - Very smooth, high CPU usage
```

### 4. **Bitrate Calculator**

```cpp
// Formula: bitrate = width * height * fps * motion_factor * quality_factor
// Example: 640x480 @ 30fps, medium quality
// bitrate = 640 * 480 * 30 * 0.07 * 0.5 ≈ 322 kb/s

// Suggested bitrates:
"bitrate=250"   // 320x240 @ 15fps
"bitrate=500"   // 640x480 @ 30fps  
"bitrate=1000"  // 720p @ 30fps
"bitrate=2000"  // 1080p @ 30fps
"bitrate=4000"  // 1080p @ 60fps
```

### 5. **Advanced Tuning Parameters**

```cpp
"x264enc "
"speed-preset=medium "      // ultrafast, superfast, veryfast, faster, fast, medium, slow, slower, veryslow
"tune=zerolatency "         // zerolatency, fastdecode, film, animation, grain, stillimage, psnr, ssim
"bitrate=1000 "             // Target bitrate in kbps
"key-int-max=30 "           // Maximum interval between keyframes (GOP size)
"vbv-buf-capacity=1000 "    // Video buffer verifier buffer capacity
"vbv-init=0.9 "             // Initial buffer occupancy
"pass=qual "                // Quality-based rate control
"quantizer=23 "             // Quantization parameter (0-51, lower=better)
" ! "
```

## Debugging Guide

### 1. **Debug Environment Variables**

```bash
# Run server with different debug levels
GST_DEBUG=1 ./rtsp_server      # Only errors
GST_DEBUG=2 ./rtsp_server      # Warnings
GST_DEBUG=3 ./rtsp_server      # Fixme messages
GST_DEBUG=4 ./rtsp_server      # Info messages
GST_DEBUG=5 ./rtsp_server      # Debug messages
GST_DEBUG=*:6 ./rtsp_server    # Log everything

# Specific element debugging
GST_DEBUG=x264enc:5,rtsp*:5 ./rtsp_server

# Color-coded debug output
GST_DEBUG_COLOR_MODE=on GST_DEBUG=3 ./rtsp_server

# Save debug logs to file
GST_DEBUG=3 ./rtsp_server 2>&1 | tee server.log
```

### 2. **Debug Commands Cheat Sheet**

```bash
# Check GStreamer installation
gst-inspect-1.0 --version

# List all available elements
gst-inspect-1.0

# Check specific element
gst-inspect-1.0 v4l2src
gst-inspect-1.0 x264enc
gst-inspect-1.0 rtph264pay

# Test webcam directly
gst-launch-1.0 v4l2src device=/dev/video0 ! videoconvert ! autovideosink

# Test pipeline step-by-step
gst-launch-1.0 v4l2src device=/dev/video0 ! videoconvert ! videoscale ! \
    video/x-raw,width=640,height=480 ! autovideosink
```

### 3. **System Diagnostics**

```bash
# Check video devices
v4l2-ctl --list-devices
v4l2-ctl --device=/dev/video0 --list-formats
v4l2-ctl --device=/dev/video0 --all

# Check CPU usage
top -p $(pgrep rtsp_server)
htop

# Check network ports
sudo netstat -tulpn | grep :8554
sudo ss -tulpn | grep :8554

# Check bandwidth usage
sudo iftop -i eth0
sudo nethogs
```

### 4. **Performance Monitoring**

```bash
# Monitor pipeline with GST_DEBUG_DUMP_DOT_DIR
mkdir -p debug_dots
GST_DEBUG_DUMP_DOT_DIR=./debug_dots GST_DEBUG=3 ./rtsp_server

# Convert dot files to images
dot -Tpng debug_dots/*.dot -o pipeline.png

# Monitor with gst-top (install first)
sudo apt install gstreamer1.0-tools
gst-top-1.0
```

## Client Testing

### 1. **Test with Various Clients**

```bash
# Using VLC (GUI)
# Open VLC → Media → Open Network Stream → rtsp://localhost:8554/webcam

# Using FFplay (CLI)
ffplay -rtsp_transport tcp rtsp://localhost:8554/webcam
ffplay -i rtsp://localhost:8554/webcam -vf "setpts=N/30"  # Force 30fps

# Using GStreamer (CLI)
gst-launch-1.0 rtspsrc location=rtsp://localhost:8554/webcam latency=0 ! \
    rtph264depay ! avdec_h264 ! videoconvert ! autovideosink

# Using OpenCV Python
# python -c "import cv2; cap = cv2.VideoCapture('rtsp://localhost:8554/webcam')"

# Using MPV player
mpv rtsp://localhost:8554/webcam --no-cache --rtsp-transport=tcp
```

### 2. **Network Testing**

```bash
# Test with different transports
# TCP (reliable, slower)
ffplay -rtsp_transport tcp rtsp://localhost:8554/webcam

# UDP (fast, may lose packets)  
ffplay -rtsp_transport udp rtsp://localhost:8554/webcam

# Test from remote machine
# On client machine:
ffplay rtsp://SERVER_IP:8554/webcam
```

## Troubleshooting

### Common Issues and Solutions

#### 1. **"Cannot open device /dev/video0"**
```bash
# Check available devices
ls -l /dev/video*

# Check permissions
ls -l /dev/video0
# Output should show: crw-rw----+ 1 root video

# Fix permissions
sudo usermod -a -G video $USER
# Log out and log back in

# Try different device
# Change in code: device=/dev/video1
```

#### 2. **"Failed to attach server to main context"**
```bash
# Port already in use
sudo lsof -i :8554
sudo kill -9 <PID>

# Or use different port
GST_RTSP_PORT=5554 ./rtsp_server
```

#### 3. **"baseline profile doesn't support 4:2:2"**
```bash
# Force 4:2:0 format
"video/x-raw,format=NV12 ! "  # or format=I420

# Or use main profile
"x264enc profile=main ! "
```

#### 4. **High CPU Usage**
```bash
# Reduce resolution
"width=320,height=240"

# Reduce frame rate  
"framerate=15/1"

# Use faster preset
"speed-preset=ultrafast"

# Lower quality
"bitrate=250"
```

#### 5. **Network Buffering/Lag**
```bash
# On server (reduce latency)
"tune=zerolatency"
"key-int-max=15"  # More frequent keyframes

# On client (VLC)
# Tools → Preferences → Show All → Input/Codecs
# Set Network caching (ms) to 100

# On client (ffplay)
ffplay -rtsp_transport tcp -buffer_size 1024000 rtsp://...
```

#### 6. **No Video/Black Screen**
```bash
# Test webcam first
gst-launch-1.0 v4l2src device=/dev/video0 ! videoconvert ! autovideosink

# Check if x264enc is available
gst-inspect-1.0 x264enc

# Install missing plugins
sudo apt install gstreamer1.0-plugins-ugly

# Run with verbose logging
GST_DEBUG=4 ./rtsp_server
```

### Quick Reference Table

| Issue | Symptoms | Solution |
|-------|----------|----------|
| No device | "Cannot open /dev/video0" | Check `v4l2-ctl --list-devices` |
| Permission denied | "Permission denied" | Add user to video group |
| Port busy | "Failed to attach" | Change port or kill process |
| Format error | "4:2:2 not supported" | Add `format=NV12` |
| High latency | Video delayed | Add `tune=zerolatency` |
| Choppy video | Dropped frames | Lower resolution/framerate |
| No video | Black screen | Test with `gst-launch-1.0` first |

### Performance Optimization Checklist

- [ ] **Resolution**: Match to use case (lower = better performance)
- [ ] **Frame Rate**: 15fps for static, 30fps for general, 60fps for fast motion
- [ ] **Bitrate**: Appropriate for network bandwidth
- [ ] **Encoding Preset**: `ultrafast` for low CPU, `medium` for balance
- [ ] **GOP Size**: Smaller = better seeking, larger = better compression
- [ ] **Color Format**: Use NV12 or I420 for H.264
- [ ] **Network Transport**: TCP for reliability, UDP for speed

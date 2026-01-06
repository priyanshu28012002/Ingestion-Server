#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QGridLayout>
#include <QDebug>
#include <QTimer>
#include <QWidget>
#include <gst/video/videooverlay.h>
#include <QShowEvent>
#include <QKeyEvent>
#include <gst/app/gstappsink.h>
#include <QImage>
#include <QPixmap>
#include <QMetaObject>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QThread>
// #include <gst/rtsp/rtsp.h>
#include <QMap>
#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>

// Global state for appsink callback (static function can't access MainWindow members)
static QMap<QLabel*, bool> g_liveStreamEnabled;  // Track which streams have live display enabled
static QMap<QLabel*, int> g_streamIndex;         // Map QLabel to stream index for motion detection
static QMap<int, QVector<uchar>> g_previousFrames;  // Store previous frame for motion detection
static QMap<int, int> g_motionFrameCount;        // Count consecutive motion/no-motion frames

// Global references to recording elements for dynamic FPS control
static QVector<GstElement*> *g_recordingEncoders = nullptr;
static QVector<bool> *g_motionDetected = nullptr;
static QVector<bool> *g_recordingActive = nullptr;

// Per-stream state for frame dropping and PTS manipulation
static QMap<int, int> g_frameDropCounter;         // Frame drop counter per stream
static QMap<int, GstClockTime> g_lastPts;         // Last PTS per stream
static QMap<int, GstClockTime> g_accumulatedPts;  // Accumulated PTS per stream
static QMap<int, bool> g_isLowFpsMode;            // Low FPS mode flag per stream

// Motion detection thresholds - Stable detection with hysteresis
#define MOTION_THRESHOLD 1.0
#define NO_MOTION_FRAMES_TO_CLOSE 40  // 40 frames = ~2 seconds (sustained no motion)
#define MOTION_FRAMES_TO_START 10     // 10 frames = ~0.5 seconds (sustained motion)

// Helper function to force keyframe
static void forceKeyframe(GstElement *encoder) {
    GstPad *sinkPad = gst_element_get_static_pad(encoder, "sink");
    if (sinkPad) {
        GstEvent *event = gst_video_event_new_downstream_force_key_unit(
            GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, FALSE, 1);
        gst_pad_send_event(sinkPad, event);
        gst_object_unref(sinkPad);
    }
}

// Pad probe callback for frame dropping and PTS manipulation
static GstPadProbeReturn frameDropAndTimestampProbe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    int streamIdx = GPOINTER_TO_INT(user_data);
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    if (!buffer) {
        return GST_PAD_PROBE_OK;
    }

    // Check if in low FPS mode for this stream
    bool isLowFps = g_isLowFpsMode.value(streamIdx, false);

    // In low FPS mode, drop 24 out of 25 frames (keep every 25th frame = ~1fps from 25fps)
    if (isLowFps) {
        int counter = g_frameDropCounter.value(streamIdx, 0);

        // If counter is negative, keep frames until we reach 0 (for keyframe capture at start)
        if (counter < 0) {
            g_frameDropCounter[streamIdx] = counter + 1;
            return GST_PAD_PROBE_OK;  // Keep this frame
        }

        counter++;
        g_frameDropCounter[streamIdx] = counter;

        if (counter < 25) {
            // Drop this frame
            return GST_PAD_PROBE_DROP;
        }
        // Keep this frame (every 25th)
        g_frameDropCounter[streamIdx] = 0;
    } else {
        // In normal mode, keep all frames
        g_frameDropCounter[streamIdx] = 0;
    }

    // Make buffer writable for timestamp manipulation
    buffer = gst_buffer_make_writable(buffer);
    GST_PAD_PROBE_INFO_DATA(info) = buffer;

    GstClockTime pts = GST_BUFFER_PTS(buffer);
    GstClockTime duration = GST_BUFFER_DURATION(buffer);

    if (pts == GST_CLOCK_TIME_NONE) {
        return GST_PAD_PROBE_OK;
    }

    GstClockTime lastPts = g_lastPts.value(streamIdx, GST_CLOCK_TIME_NONE);
    GstClockTime accumulatedPts = g_accumulatedPts.value(streamIdx, 0);

    if (lastPts == GST_CLOCK_TIME_NONE) {
        // First buffer after recording started - start from PTS 0 for clean recording
        g_lastPts[streamIdx] = pts;
        g_accumulatedPts[streamIdx] = 0;  // Always start recording from PTS 0
        GST_BUFFER_PTS(buffer) = 0;        // First frame at PTS 0
        // Don't set duration - let muxer/player calculate from PTS differences
        GST_BUFFER_DURATION(buffer) = GST_CLOCK_TIME_NONE;
        return GST_PAD_PROBE_OK;
    }

    // Calculate time delta since last frame
    GstClockTime delta;
    if (pts >= lastPts) {
        delta = pts - lastPts;
    } else {
        // Handle discontinuity - keep accumulated PTS continuous, just update lastPts
        g_lastPts[streamIdx] = pts;
        // Don't reset accumulatedPts - keep it continuous
        GST_BUFFER_PTS(buffer) = accumulatedPts;
        return GST_PAD_PROBE_OK;
    }

    g_lastPts[streamIdx] = pts;

    if (isLowFps) {
        // Compress time by 10x: 1 second becomes 0.1 seconds (10x playback)
        delta = delta / 10;
    }

    accumulatedPts += delta;
    g_accumulatedPts[streamIdx] = accumulatedPts;
    GST_BUFFER_PTS(buffer) = accumulatedPts;
    // Don't set duration - let muxer/player calculate from PTS differences
    GST_BUFFER_DURATION(buffer) = GST_CLOCK_TIME_NONE;

    return GST_PAD_PROBE_OK;
}

// ------------------- appsink: convert sample -> QImage and push to QLabel -------------------
static GstFlowReturn on_new_sample_from_sink(GstAppSink *appsink, gpointer user_data) {

    // Skip first few frames (often corrupted during stream start)
    static QMap<QLabel*, int> frameCount;
    static bool firstFrameLogged = false;

    if (!user_data) {
        GstSample *s = gst_app_sink_pull_sample(appsink);
        if (s) gst_sample_unref(s);
        return GST_FLOW_OK;
    }

    QLabel *label = static_cast<QLabel*>(user_data);

    // Check if live stream display is enabled for this label
    if (g_liveStreamEnabled.contains(label) && !g_liveStreamEnabled[label]) {
        // Live stream disabled - just drop the frame without rendering
        GstSample *s = gst_app_sink_pull_sample(appsink);
        if (s) gst_sample_unref(s);
        return GST_FLOW_OK;
    }

    if (!frameCount.contains(label)) {
        frameCount[label] = 0;
    }
    frameCount[label]++;

    // Log first frame arrival for debugging
    if (!firstFrameLogged) {
        qDebug() << "[APPSINK] First frame arrived at appsink callback - frame count:" << frameCount[label];
        firstFrameLogged = true;
    }

    if (frameCount[label] < 5) {
        GstSample *s = gst_app_sink_pull_sample(appsink);
        if (s) gst_sample_unref(s);
        return GST_FLOW_OK;
    }

    GstSample *sample = gst_app_sink_pull_sample(appsink);
    if (!sample) return GST_FLOW_OK;

    GstCaps *caps = gst_sample_get_caps(sample);
    if (!caps) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstStructure *s = gst_caps_get_structure(caps, 0);
    int width = 0, height = 0;
    gst_structure_get_int(s, "width", &width);
    gst_structure_get_int(s, "height", &height);

    if (width <= 0 || height <= 0) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    // Skip corrupted/incomplete frames
    if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_CORRUPTED)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstMapInfo map;
    gsize expected_size = (gsize)width * height * 3;

    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    // Validate buffer size
    if (map.size < expected_size) {
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    // Check for green/black frame (first 1000 bytes mostly zeros = corrupted)
    int zero_count = 0;
    int check_bytes = (map.size < 1000) ? map.size : 1000;
    for (int i = 0; i < check_bytes; i++) {
        if (map.data[i] == 0) zero_count++;
    }
    if (zero_count > check_bytes * 0.5) {  // More than 90% zeros = bad frame
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    // ========== MOTION DETECTION ==========
    // Only do motion detection if we have valid global references and recording is active
    if (g_streamIndex.contains(label) && g_recordingActive && g_recordingEncoders) {
        int streamIdx = g_streamIndex[label];

        // Only process if recording is active for this stream
        if (streamIdx < g_recordingActive->size() && (*g_recordingActive)[streamIdx]) {
            // Downsample frame for faster motion detection (every 4th pixel)
            int sampleSize = width * height / 16;  // Sample 1/16 of pixels
            QVector<uchar> currentSample(sampleSize);

            for (int i = 0, j = 0; i < (int)map.size && j < sampleSize; i += 48, j++) {  // 48 = 4 pixels * 3 channels
                currentSample[j] = map.data[i];  // Just take R channel
            }

            // Compare with previous frame
            bool hasMotion = false;
            if (g_previousFrames.contains(streamIdx) && g_previousFrames[streamIdx].size() == sampleSize) {
                int diffCount = 0;
                int threshold = 30;  // Pixel difference threshold

                for (int i = 0; i < sampleSize; i++) {
                    int diff = qAbs(currentSample[i] - g_previousFrames[streamIdx][i]);
                    if (diff > threshold) {
                        diffCount++;
                    }
                }

                // Motion detected if >1% of sampled pixels changed (MOTION_THRESHOLD = 1.0)
                float motionPercent = (float)diffCount / sampleSize * 100.0f;
                hasMotion = (motionPercent > MOTION_THRESHOLD);

                // Frame-by-frame logging
                qDebug() << "[Frame] Stream" << streamIdx
                         << "- Motion:" << (hasMotion ? "YES" : "NO")
                         << "| Changed:" << QString::number(motionPercent, 'f', 2) << "%";
            }

            // Store current frame for next comparison
            g_previousFrames[streamIdx] = currentSample;

            // Track consecutive frames to avoid flickering between states
            if (!g_motionFrameCount.contains(streamIdx)) {
                g_motionFrameCount[streamIdx] = 0;
            }

            int &motionCount = g_motionFrameCount[streamIdx];
            static QMap<int, int> noMotionCount;  // Track no-motion frames

            if (hasMotion) {
                noMotionCount[streamIdx] = 0;
                motionCount++;
            } else {
                motionCount = 0;
                noMotionCount[streamIdx]++;
            }

            // Get current motion state
            bool currentlyInMotion = !g_isLowFpsMode.value(streamIdx, true);  // Default to low fps mode

            // Log counter progress
            QString mode = currentlyInMotion ? "NORMAL_FPS" : "LOW_FPS";
            qDebug() << "       Mode:" << mode
                     << "| Motion frames:" << motionCount << "/" << MOTION_FRAMES_TO_START
                     << "| No-motion frames:" << noMotionCount[streamIdx] << "/" << NO_MOTION_FRAMES_TO_CLOSE;

            // Switch to NORMAL FPS if motion detected consistently
            if (!currentlyInMotion && motionCount >= MOTION_FRAMES_TO_START) {
                g_isLowFpsMode[streamIdx] = true;  // Normal FPS mode
                g_frameDropCounter[streamIdx] = -5;
                (*g_motionDetected)[streamIdx] = true;
                qDebug() << "";
                qDebug() << "🔴 MOTION DETECTED - Stream" << streamIdx << "→ Switching to NORMAL FPS (20fps), normal speed";
                qDebug() << "";
            }
            // Switch to LOW FPS if no motion for sustained period
            else if (currentlyInMotion && noMotionCount[streamIdx] >= NO_MOTION_FRAMES_TO_CLOSE) {
                g_isLowFpsMode[streamIdx] = true;  // Low FPS mode
                g_frameDropCounter[streamIdx] = 0;
                (*g_motionDetected)[streamIdx] = false;
                qDebug() << "";
                qDebug() << "⚪ NO MOTION - Stream" << streamIdx << "→ Switching to LOW FPS (1fps), 10x playback speed";
                qDebug() << "";
            }
        }
    }
    // ========== END MOTION DETECTION ==========

    QImage img(map.data, width, height, width * 3, QImage::Format_RGB888);
    QImage copy = img.copy();  // Deep copy before unmap

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);

    if (!copy.isNull()) {
        QMetaObject::invokeMethod(label, [label, copy]() {
            label->setPixmap(QPixmap::fromImage(copy).scaled(
                label->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
        }, Qt::QueuedConnection);
    }

    return GST_FLOW_OK;
}

// ------------------- rtspsrc pad-added handler -------------------
static void on_rtspsrc_pad_added(GstElement *src, GstPad *new_pad, gpointer user_data)
{
    Q_UNUSED(src);

    GstElement *depay = GST_ELEMENT(user_data);
    GstPad *sink_pad = gst_element_get_static_pad(depay, "sink");

    if (gst_pad_is_linked(sink_pad)) {
        gst_object_unref(sink_pad);
        return;
    }

    GstCaps *caps = gst_pad_query_caps(new_pad, NULL);
    gchar *caps_str = gst_caps_to_string(caps);
    qDebug() << "[rtspsrc pad-added] Caps:" << caps_str;
    g_free(caps_str);
    gst_caps_unref(caps);

    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
    if (ret == GST_PAD_LINK_OK) {
        qDebug() << "Linked rtspsrc -> rtph265depay successfully";
    } else {
        qWarning() << "Failed to link rtspsrc -> rtph265depay, error:" << ret;
    }

    gst_object_unref(sink_pad);
}

// ===== decodebin pad-added handler =====
static void on_decodebin_pad_added(GstElement *decodebin, GstPad *pad, gpointer user_data)
{
    Q_UNUSED(decodebin);

    GstElement *queue = GST_ELEMENT(user_data);

    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) {
        caps = gst_pad_query_caps(pad, NULL);
    }

    if (caps) {
        gchar *caps_str = gst_caps_to_string(caps);
        qDebug() << "[decodebin pad-added] Caps:" << caps_str;

        GstStructure *str = gst_caps_get_structure(caps, 0);
        const gchar *name = gst_structure_get_name(str);

        if (!g_str_has_prefix(name, "video/")) {
            qDebug() << "Ignoring non-video pad";
            g_free(caps_str);
            gst_caps_unref(caps);
            return;
        }
        g_free(caps_str);
        gst_caps_unref(caps);
    }

    GstPad *sinkpad = gst_element_get_static_pad(queue, "sink");

    if (!gst_pad_is_linked(sinkpad)) {
        GstPadLinkReturn ret = gst_pad_link(pad, sinkpad);
        if (ret == GST_PAD_LINK_OK) {
            qDebug() << "Linked decodebin -> queue successfully";
        } else {
            qWarning() << "Failed to link decodebin -> queue, error:" << ret;
        }
    }

    gst_object_unref(sinkpad);
}

// Bus message handler
static gboolean bus_callback(GstBus *bus, GstMessage *msg, gpointer data)
{
    Q_UNUSED(bus);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);
        qWarning() << "GStreamer Error:" << (err ? err->message : "unknown");
        qWarning() << "Debug info:" << (debug ? debug : "none");
        g_error_free(err);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_WARNING: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_warning(msg, &err, &debug);
        qWarning() << "GStreamer Warning:" << (err ? err->message : "unknown");
        g_error_free(err);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_EOS:
        qDebug() << "End of stream";
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        GstState old_state, new_state;
        gst_message_parse_state_changed(msg, &old_state, &new_state, NULL);
        if (GST_MESSAGE_SRC(msg) == data) {
            qDebug() << "Pipeline state changed from"
                     << gst_element_state_get_name(old_state)
                     << "to" << gst_element_state_get_name(new_state);
        }
        break;
    }
    default:
        break;
    }
    return TRUE;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_currentGridMode(GridSettingsDialog::Grid2x2)
    , m_fullscreenStreamIndex(-1)
    , m_isFullscreen(false)
    , m_fullscreenWidget(nullptr)
{
    ui->setupUi(this);

    // Initialize GStreamer (PIPELINE CODE - DO NOT MODIFY)
    setupGStreamer();

    // Register custom types for Qt signal/slot system
    qRegisterMetaType<RecorderSettings>("RecorderSettings");

    // Load settings first
    loadSettings();

    // Initialize vectors for 4 streams (expandable to 9 later)
    const int initialStreamCount = 4;
    videoContainers.resize(initialStreamCount);
    videoLabels.resize(initialStreamCount);
    streamOverlays.resize(initialStreamCount);
    pipelines.resize(initialStreamCount);
    m_recorderThreads.resize(initialStreamCount);
    m_recorderWorkers.resize(initialStreamCount);

    // Set up NEW UI (overlays, grid, settings button)
    setupUI();
    setupVideoGrid();

    // Set up motion recorders (PIPELINE CODE - DO NOT MODIFY)
    // DISABLED: Old worker system - recording now handled by TEE pipeline VALVE
    // setupMotionRecorders();

    setWindowTitle("Qt GStreamer RTSP - Multi-Camera Viewer");
}

MainWindow::~MainWindow()
{
    // DISABLED: Old worker system - recording now handled by TEE pipeline VALVE
    // Stop all motion recorders first
    // for (int i = 0; i < m_recorderWorkers.size(); ++i) {
    //     if (m_recorderWorkers[i]) {
    //         stopMotionRecorder(i);
    //     }
    // }

    // Stop all worker threads
    // for (int i = 0; i < m_recorderThreads.size(); ++i) {
    //     if (m_recorderThreads[i]) {
    //         m_recorderThreads[i]->quit();
    //         if (!m_recorderThreads[i]->wait(3000)) {
    //             qWarning() << "Worker thread" << i << "did not quit gracefully, terminating";
    //             m_recorderThreads[i]->terminate();
    //             m_recorderThreads[i]->wait();
    //         }
    //         delete m_recorderThreads[i];
    //         m_recorderThreads[i] = nullptr;
    //     }
    // }

    // Clean up workers (they're owned by threads, but delete explicitly to be safe)
    // for (int i = 0; i < m_recorderWorkers.size(); ++i) {
    //     if (m_recorderWorkers[i]) {
    //         delete m_recorderWorkers[i];
    //         m_recorderWorkers[i] = nullptr;
    //     }
    // }

    // Stop live streams
    stopStreams();
    gst_deinit();
    delete ui;
}

void MainWindow::setupGStreamer()
{
    // Set GStreamer plugin paths before gst_init()
    qputenv("GST_PLUGIN_PATH", "/usr/local/lib/gstreamer-1.0:/usr/lib/x86_64-linux-gnu/gstreamer-1.0");
    qputenv("GST_PLUGIN_SYSTEM_PATH", "/usr/local/lib/gstreamer-1.0:/usr/lib/x86_64-linux-gnu/gstreamer-1.0");

    gst_init(nullptr, nullptr);
    qDebug() << "GStreamer initialized successfully.";

    // Verify h265parse is available
    GstElementFactory *factory = gst_element_factory_find("h265parse");
    if (factory) {
        qDebug() << "h265parse plugin found successfully";
        gst_object_unref(factory);
    } else {
        qWarning() << "WARNING: h265parse plugin NOT found!";
    }
}

void MainWindow::startStream(int index, const char *uri)
{
    if (index < 0) return;

    if (pipelines.size() <= index) pipelines.resize(index + 1);

    if (pipelines[index]) {
        gst_element_set_state(pipelines[index], GST_STATE_NULL);
        gst_object_unref(pipelines[index]);
        pipelines[index] = nullptr;
    }

    createPipelineForStream(index, uri);
}

void MainWindow::createPipelineForStream(int index, const char *uri)
{
    if (index < 0 || index >= pipelines.size()) return;

    if (pipelines[index]) {
        gst_element_set_state(pipelines[index], GST_STATE_NULL);
        gst_object_unref(pipelines[index]);
        pipelines[index] = nullptr;
    }

    QString name = QString("stream%1_").arg(index);

    GstElement *pipeline   = gst_pipeline_new((name + "pipeline").toUtf8().constData());
    GstElement *source     = gst_element_factory_make("rtspsrc",       (name + "source").toUtf8().constData());
    GstElement *depay      = gst_element_factory_make("rtph265depay",  (name + "depay").toUtf8().constData());
    GstElement *queue_net  = gst_element_factory_make("queue", (name + "queue_net").toUtf8().constData());
    GstElement *parse      = gst_element_factory_make("h265parse",     (name + "parse").toUtf8().constData());
    GstElement *decoder    = gst_element_factory_make("decodebin",     (name + "decoder").toUtf8().constData());
    GstElement *queue      = gst_element_factory_make("queue",         (name + "queue").toUtf8().constData());
    GstElement *convert    = gst_element_factory_make("videoconvert",  (name + "convert").toUtf8().constData());
    GstElement *scale      = gst_element_factory_make("videoscale",    (name + "scale").toUtf8().constData());
    GstElement *capsfilter = gst_element_factory_make("capsfilter",    (name + "capsfilter").toUtf8().constData());
    GstElement *appsink    = gst_element_factory_make("appsink",       (name + "appsink").toUtf8().constData());
    GstElement *rate      = gst_element_factory_make("videorate",    (name + "rate").toUtf8().constData());

    // ============ NEW: TEE + RECORDING BRANCH ELEMENTS ============
    GstElement *tee           = gst_element_factory_make("tee",           (name + "tee").toUtf8().constData());
    GstElement *queue_live    = gst_element_factory_make("queue",         (name + "queue_live").toUtf8().constData());
    GstElement *queue_record  = gst_element_factory_make("queue",         (name + "queue_record").toUtf8().constData());
    GstElement *valve_rec     = gst_element_factory_make("valve",         (name + "valve_rec").toUtf8().constData());  // Control data flow
    GstElement *convert_rec   = gst_element_factory_make("videoconvert",  (name + "convert_rec").toUtf8().constData());
    GstElement *scale_rec     = gst_element_factory_make("videoscale",    (name + "scale_rec").toUtf8().constData());
    GstElement *rate_rec      = gst_element_factory_make("videorate",     (name + "rate_rec").toUtf8().constData());   // Variable FPS for motion (after convert/scale)
    GstElement *caps_rec      = gst_element_factory_make("capsfilter",    (name + "caps_rec").toUtf8().constData());
    GstElement *encoder       = gst_element_factory_make("nvh265enc",     (name + "encoder").toUtf8().constData());
    GstElement *parse_enc     = gst_element_factory_make("h265parse",     (name + "parse_enc").toUtf8().constData());
    GstElement *muxer         = gst_element_factory_make("matroskamux",   (name + "muxer").toUtf8().constData());
    GstElement *fakesink      = gst_element_factory_make("fakesink",      (name + "fakesink").toUtf8().constData());
    // filesink will be created dynamically when recording starts
    // ============ END NEW ELEMENTS ============

    // Debug: Check which element failed
    if (!pipeline) qWarning() << "Failed to create pipeline for stream" << index;
    if (!source) qWarning() << "Failed to create rtspsrc for stream" << index;
    if (!rate_rec) qWarning() << "Failed to create videorate for recording stream" << index;
    if (!depay) qWarning() << "Failed to create rtph265depay for stream" << index;
    if (!queue_net) qWarning() << "Failed to create queue_net for stream" << index;
    if (!parse) qWarning() << "Failed to create h265parse for stream" << index;
    if (!decoder) qWarning() << "Failed to create decodebin for stream" << index;
    if (!queue) qWarning() << "Failed to create queue for stream" << index;
    if (!convert) qWarning() << "Failed to create videoconvert for stream" << index;
    if (!scale) qWarning() << "Failed to create videoscale for stream" << index;
    if (!rate) qWarning() << "Failed to create videorate for stream" << index;
    if (!capsfilter) qWarning() << "Failed to create capsfilter for stream" << index;
    if (!appsink) qWarning() << "Failed to create appsink for stream" << index;

    if (!pipeline || !source || !depay || !parse || !decoder || !queue || !convert || !scale || !rate || !capsfilter || !appsink || !queue_net) {
        qWarning() << "Pipeline element creation failed for stream" << index;
        if (pipeline) gst_object_unref(pipeline);
        return;
    }

    // decodebin will auto-select best decoder (nvdec for CUDA or software decoder)

    // ============ OPTIMIZED LOW LATENCY SETTINGS ============

    // Configure rtspsrc - ULTRA LOW LATENCY for real-time viewing
    g_object_set(source,
                 "location", uri,
                 "latency", 50,                // 50ms - ultra low latency
                 "buffer-mode", 1,             // Auto buffering mode
                 "do-retransmission", FALSE,   // No retransmission (real-time priority)
                 "drop-on-latency", TRUE,      // Drop old packets to stay live
                 "timeout", (guint64)5000000,  // 5 seconds timeout
                 "ntp-sync", FALSE,            // Disable NTP for lower latency
                 nullptr);

    // Set UDP protocol for low latency real-time streaming
    GValue val = G_VALUE_INIT;
    g_value_init(&val, G_TYPE_UINT);
    g_value_set_uint(&val, 0x1);  // 0x1 = UDP (low latency, real-time)
    g_object_set_property(G_OBJECT(source), "protocols", &val);
    g_value_unset(&val);

    // Network queue - MINIMAL buffering for lowest latency
    g_object_set(queue_net,
                 "max-size-buffers", 3,                 // Only 3 frames (~150ms @ 20fps)
                 "max-size-time", (guint64)150000000,   // 150ms buffer
                 "max-size-bytes", 2097152,             // 2MB buffer
                 "leaky", 2,                            // Drop old frames if buffer full
                 "flush-on-eos", TRUE,
                 nullptr);

    // Decoder output queue - ULTRA LIGHT buffering for immediate display
    g_object_set(queue,
                 "max-size-buffers", 2,                 // Just 2 decoded frames (~100ms)
                 "max-size-time", (guint64)100000000,   // 100ms max
                 "max-size-bytes", 0,                   // No byte limit
                 "leaky", 2,                            // Drop old frames to stay current
                 nullptr);

    // Scale down to reduce CPU load - 640x360 is enough for a grid view
    // GstCaps *rgbcaps = gst_caps_new_simple(
    //     "video/x-raw",
    //     "format", G_TYPE_STRING, "RGB",
    //     "width", G_TYPE_INT, 1280,
    //     "height", G_TYPE_INT, 720,
    //     nullptr);

    GstCaps *rgbcaps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, "RGB",
        nullptr);
    g_object_set(capsfilter, "caps", rgbcaps, nullptr);
    gst_caps_unref(rgbcaps);

    // Configure appsink for low-latency display
    g_object_set(appsink,
                 "emit-signals", FALSE,
                 "sync", FALSE,               // Don't sync to clock - display ASAP
                 "async", FALSE,              // Don't block pipeline state changes
                 "max-buffers", 1,            // Only keep latest frame
                 "drop", TRUE,                // Drop if we can't keep up
                 nullptr);

    // ============ CONFIGURE RECORDING BRANCH ============

    // Configure tee - allow-not-linked so branches don't block each other
    g_object_set(tee,
                 "allow-not-linked", TRUE,
                 nullptr);

    // Configure valve - START CLOSED (recording OFF initially)
    g_object_set(valve_rec,
                 "drop", TRUE,  // Drop all buffers (recording OFF)
                 nullptr);

    // Recording queue - buffering for encoder
    g_object_set(queue_record,
                 "max-size-buffers", 10,
                 "max-size-time", (guint64)1000000000,  // 1 second
                 "leaky", 0,  // Don't drop frames
                 nullptr);

    // Configure recording resolution (using settings from RecorderSettings)
    int recordWidth = (index < m_globalSettings.recorderSettings.size()) ?
                      m_globalSettings.recorderSettings[index].width : 640;
    int recordHeight = (index < m_globalSettings.recorderSettings.size()) ?
                       m_globalSettings.recorderSettings[index].height : 360;
    int recordBitrate = (index < m_globalSettings.recorderSettings.size()) ?
                        m_globalSettings.recorderSettings[index].bitrateKbps : 800;
    int lowFps = (index < m_globalSettings.recorderSettings.size()) ?
                 m_globalSettings.recorderSettings[index].lowFps : 1;

    // Configure videorate - DISABLED for now (passthrough mode)
    // Matroska doesn't support caps changes, so no dynamic FPS switching
    g_object_set(rate_rec,
                 "drop-only", FALSE,       // Passthrough mode - don't drop frames
                 nullptr);

    // Configure caps - let videorate control framerate dynamically
    // Only specify resolution here
    GstCaps *record_caps = gst_caps_new_simple(
        "video/x-raw",
        "width", G_TYPE_INT, recordWidth,
        "height", G_TYPE_INT, recordHeight,
        nullptr);
    g_object_set(caps_rec, "caps", record_caps, nullptr);
    gst_caps_unref(record_caps);

    // Configure nvh265enc encoder
    g_object_set(encoder,
                 "bitrate", recordBitrate,
                 "preset", 2,  // Low latency HP
                 "gop-size", 30,
                 "zerolatency", TRUE,
                 nullptr);

    // Configure fakesink - used when not recording (no file created)
    g_object_set(fakesink,
                 "sync", FALSE,
                 "async", FALSE,  // Don't block pipeline state changes
                 nullptr);

    qDebug() << "Recording branch configured for stream" << index << "- using fakesink (no file until recording starts)";

    // ============ END RECORDING CONFIGURATION ============

    // Add all elements to pipeline (LIVE + RECORDING branches)
    gst_bin_add_many(GST_BIN(pipeline),
                     source, depay, queue_net, parse, decoder,
                     tee, queue_live, queue, convert, scale, rate, capsfilter, appsink,
                     queue_record, valve_rec, rate_rec, convert_rec, scale_rec, caps_rec, encoder, parse_enc, muxer, fakesink,
                     nullptr);

    // Static linking: depay -> parse -> decoder
    if (!gst_element_link_many(depay, queue_net, parse, decoder, nullptr)) {
        qWarning() << "Failed to link depay -> parse -> decoder for stream" << index;
        gst_object_unref(pipeline);
        return;
    }

    // ============ TEE BRANCH LINKING ============

    // LIVE BRANCH: tee -> queue_live -> queue -> convert -> scale -> rate -> capsfilter -> appsink
    if (!gst_element_link_many(queue_live, queue, convert, scale, rate, capsfilter, appsink, nullptr)) {
        qWarning() << "Failed to link LIVE branch for stream" << index;
        gst_object_unref(pipeline);
        return;
    }

    // RECORDING BRANCH: tee -> queue_record -> VALVE -> convert_rec -> scale_rec -> videorate -> caps_rec -> encoder -> parse_enc -> muxer -> fakesink
    if (!gst_element_link_many(queue_record, valve_rec, convert_rec, scale_rec, rate_rec, caps_rec, encoder, parse_enc, muxer, fakesink, nullptr)) {
        qWarning() << "Failed to link RECORDING branch for stream" << index;
        gst_object_unref(pipeline);
        return;
    }

    // Request pads from tee for both branches
    GstPad *tee_live_pad = gst_element_request_pad_simple(tee, "src_%u");
    GstPad *tee_record_pad = gst_element_request_pad_simple(tee, "src_%u");
    GstPad *queue_live_sink = gst_element_get_static_pad(queue_live, "sink");
    GstPad *queue_record_sink = gst_element_get_static_pad(queue_record, "sink");

    if (gst_pad_link(tee_live_pad, queue_live_sink) != GST_PAD_LINK_OK) {
        qWarning() << "Failed to link tee -> queue_live for stream" << index;
        gst_object_unref(pipeline);
        return;
    }

    if (gst_pad_link(tee_record_pad, queue_record_sink) != GST_PAD_LINK_OK) {
        qWarning() << "Failed to link tee -> queue_record for stream" << index;
        gst_object_unref(pipeline);
        return;
    }

    gst_object_unref(tee_live_pad);
    gst_object_unref(tee_record_pad);
    gst_object_unref(queue_live_sink);
    gst_object_unref(queue_record_sink);

    qDebug() << "TEE branches linked successfully for stream" << index;

    // ============ END TEE LINKING ============

    // Dynamic linking: rtspsrc -> depay (RTSP has dynamic pads)
    g_signal_connect(source, "pad-added", G_CALLBACK(on_rtspsrc_pad_added), depay);

    // Dynamic linking: decoder -> tee (decodebin has dynamic pads) - CHANGED FROM queue to tee!
    g_signal_connect(decoder, "pad-added", G_CALLBACK(on_decodebin_pad_added), tee);

    // Set up appsink callbacks
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = on_new_sample_from_sink;
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, videoLabels[index], nullptr);

    // Initialize live stream as enabled (button starts green)
    g_liveStreamEnabled[videoLabels[index]] = true;

    // Initialize motion detection mappings
    g_streamIndex[videoLabels[index]] = index;

    // Initialize global pointers for motion detection (only once)
    if (!g_recordingEncoders) {
        g_recordingEncoders = &recordingEncoders;
        g_motionDetected = &motionDetected;
        g_recordingActive = &recordingActive;
    }

    // Initialize per-stream state for motion detection
    // Start in NORMAL FPS mode to avoid initial frame freeze
    g_isLowFpsMode[index] = false;  // Start in normal FPS mode
    g_frameDropCounter[index] = -5;
    g_lastPts[index] = GST_CLOCK_TIME_NONE;
    g_accumulatedPts[index] = 0;

    // Install pad probe on encoder sink pad for frame dropping and PTS manipulation
    GstPad *encoderSinkPad = gst_element_get_static_pad(encoder, "sink");
    if (encoderSinkPad) {
        gst_pad_add_probe(encoderSinkPad, GST_PAD_PROBE_TYPE_BUFFER,
                         (GstPadProbeCallback) frameDropAndTimestampProbe,
                         GINT_TO_POINTER(index), nullptr);
        gst_object_unref(encoderSinkPad);
        qDebug() << "Frame drop and timestamp probe installed for stream" << index;
    }

    // Add bus watch
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_callback, pipeline);
    gst_object_unref(bus);

    // Start the pipeline
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "Failed to start pipeline for stream" << index;
        gst_object_unref(pipeline);
        return;
    }

    pipelines[index] = pipeline;

    // Store recording elements references for toggle control
    if (recordingValves.size() <= index) {
        recordingValves.resize(index + 1, nullptr);
        recordingVideorates.resize(index + 1, nullptr);
        recordingCapsfilters.resize(index + 1, nullptr);
        recordingEncoders.resize(index + 1, nullptr);
        recordingParsers.resize(index + 1, nullptr);
        recordingMuxers.resize(index + 1, nullptr);
        recordingFilesinks.resize(index + 1, nullptr);
        recordingFakesinks.resize(index + 1, nullptr);
        recordingActive.resize(index + 1, false);
        liveStreamActive.resize(index + 1, true);   // Start with live stream ON
        motionDetected.resize(index + 1, false);     // Start with no motion
    }
    recordingValves[index] = valve_rec;
    recordingVideorates[index] = rate_rec;
    recordingCapsfilters[index] = caps_rec;
    recordingEncoders[index] = encoder;
    recordingParsers[index] = parse_enc;
    recordingMuxers[index] = muxer;
    recordingFilesinks[index] = nullptr;  // Will be created dynamically when recording starts
    recordingFakesinks[index] = fakesink;
    recordingActive[index] = false;  // Start with recording OFF (valve closed)
    liveStreamActive[index] = true;  // Start with live stream ON (button is green by default)
    motionDetected[index] = false;   // Start with no motion (1fps recording)

    qDebug() << "Pipeline started for stream" << index << "URI:" << uri;
    qDebug() << "Recording branch PAUSED (use toggle to start recording)";
}

void MainWindow::stopStream(int index)
{
    if (index < 0 || index >= pipelines.size()) return;
    if (!pipelines[index]) return;

    gst_element_set_state(pipelines[index], GST_STATE_NULL);
    gst_object_unref(pipelines[index]);
    pipelines[index] = nullptr;

    if (index < videoLabels.size() && videoLabels[index]) {
        QMetaObject::invokeMethod(videoLabels[index], [lab = videoLabels[index]]() {
            lab->clear();
            lab->setText("Stopped");
        }, Qt::QueuedConnection);
    }
}

void MainWindow::stopStreams()
{
    for (int i = 0; i < pipelines.size(); ++i) {
        stopStream(i);
    }
}

// OLD_UI: void MainWindow::setupControlBar()
// OLD_UI: {
// OLD_UI:     m_controlBar = new QWidget(this);
// OLD_UI:     QVBoxLayout *controlLayout = new QVBoxLayout(m_controlBar);
// OLD_UI: 
// OLD_UI:     // Settings button at top
// OLD_UI:     m_settingsButton = new QPushButton("Settings", m_controlBar);
// OLD_UI:     connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::onSettingsButtonClicked);
// OLD_UI:     controlLayout->addWidget(m_settingsButton);
// OLD_UI: 
// OLD_UI:     // 4 rows for 4 streams
// OLD_UI:     for (int i = 0; i < 4; ++i) {
// OLD_UI:         QHBoxLayout *rowLayout = new QHBoxLayout();
// OLD_UI: 
// OLD_UI:         // Stream checkbox
// OLD_UI:         m_streamCheckboxes[i] = new QCheckBox(QString("Stream %1 Live").arg(i + 1), m_controlBar);
// OLD_UI:         m_streamCheckboxes[i]->setChecked(true);  // Start with streams visible
// OLD_UI:         connect(m_streamCheckboxes[i], &QCheckBox::toggled, this, [this, i](bool checked) {
// OLD_UI:             onStreamCheckboxToggled(i, checked);
// OLD_UI:         });
// OLD_UI:         rowLayout->addWidget(m_streamCheckboxes[i]);
// OLD_UI: 
// OLD_UI:         // Record checkbox
// OLD_UI:         m_recordCheckboxes[i] = new QCheckBox("Record Motion", m_controlBar);
// OLD_UI:         m_recordCheckboxes[i]->setChecked(m_globalSettings.recorderSettings[i].enabled);
// OLD_UI:         connect(m_recordCheckboxes[i], &QCheckBox::toggled, this, [this, i](bool checked) {
// OLD_UI:             onRecordCheckboxToggled(i, checked);
// OLD_UI:         });
// OLD_UI:         rowLayout->addWidget(m_recordCheckboxes[i]);
// OLD_UI: 
// OLD_UI:         // Status label
// OLD_UI:         m_statusLabels[i] = new QLabel("Idle", m_controlBar);
// OLD_UI:         m_statusLabels[i]->setStyleSheet("color: gray;");
// OLD_UI:         m_statusLabels[i]->setMinimumWidth(150);
// OLD_UI:         rowLayout->addWidget(m_statusLabels[i]);
// OLD_UI: 
// OLD_UI:         rowLayout->addStretch();
// OLD_UI:         controlLayout->addLayout(rowLayout);
// OLD_UI:     }
// OLD_UI: 
// OLD_UI:     m_controlBar->setLayout(controlLayout);
// OLD_UI: }

void MainWindow::setupMotionRecorders()
{
    // Create worker threads and workers
    for (int i = 0; i < 4; ++i) {
        // Create thread
        m_recorderThreads[i] = new QThread(this);

        // Create worker
        m_recorderWorkers[i] = new MotionRecorderWorker(i);

        // Move worker to thread
        m_recorderWorkers[i]->moveToThread(m_recorderThreads[i]);

        // Connect worker signals to UI slots
        connect(m_recorderWorkers[i], &MotionRecorderWorker::statusUpdate, this, [this, i](const QString &status) {
            onRecorderStatusUpdate(i, status);
        });
        connect(m_recorderWorkers[i], &MotionRecorderWorker::motionDetected, this, [this, i](bool hasMotion) {
            onMotionDetected(i, hasMotion);
        });
        connect(m_recorderWorkers[i], &MotionRecorderWorker::errorOccurred, this, [this, i](const QString &error) {
            onRecorderError(i, error);
        });

        // Start thread
        m_recorderThreads[i]->start();

        qDebug() << "Motion recorder worker" << i << "initialized on thread";
    }
}

void MainWindow::loadSettings()
{
    QSettings settings("MotionRecorder", "QtGStreamer");

    // Create Recordings directory in /workspace (accessible on host)
    QString recordingsDir = "/workspace/Recordings";
    QDir dir;
    if (!dir.exists(recordingsDir)) {
        if (dir.mkpath(recordingsDir)) {
            qDebug() << "Created Recordings directory at:" << recordingsDir;
        } else {
            qDebug() << "Failed to create Recordings directory at:" << recordingsDir;
        }
    }

    // Load global settings
    m_globalSettings.autoStart = settings.value("autoStart", false).toBool();

    // Load per-stream settings (support up to 9 streams for 3x3 grid)
    int maxStreams = m_globalSettings.recorderSettings.size();
    for (int i = 0; i < maxStreams; ++i) {
        QString prefix = QString("stream%1/").arg(i);
        RecorderSettings &rs = m_globalSettings.recorderSettings[i];

        // Camera Details
        rs.cameraName = settings.value(prefix + "cameraName", QString("Camera %1").arg(i+1)).toString();
        rs.rtspUrl = settings.value(prefix + "rtspUrl", "").toString();

        // Default path: /workspace/Recordings (accessible on host)
        QString defaultPath = "/workspace/Recordings/" + QString("motion_recording_%1.mkv").arg(i);
        rs.outputPath = settings.value(prefix + "outputPath", defaultPath).toString();

        // Live Stream Settings
        rs.liveStreamWidth = settings.value(prefix + "liveStreamWidth", 1280).toInt();
        rs.liveStreamHeight = settings.value(prefix + "liveStreamHeight", 720).toInt();
        rs.liveStreamFps = settings.value(prefix + "liveStreamFps", 15).toInt();

        // Recording Settings
        rs.bitrateKbps = settings.value(prefix + "bitrate", 1000).toInt();
        rs.normalFps = settings.value(prefix + "normalFps", 25).toInt();
        rs.lowFps = settings.value(prefix + "lowFps", 1).toInt();
        rs.motionThreshold = settings.value(prefix + "motionThreshold", 1.0).toDouble();
        rs.width = settings.value(prefix + "width", 1280).toInt();
        rs.height = settings.value(prefix + "height", 720).toInt();
        rs.motionFramesToStart = settings.value(prefix + "motionFramesToStart", 8).toInt();
        rs.noMotionFramesToStop = settings.value(prefix + "noMotionFramesToStop", 50).toInt();
        rs.enabled = settings.value(prefix + "enabled", false).toBool();

        // Set RTSP URLs based on stream index (same as hardcoded in showEvent)
        if (rs.rtspUrl.isEmpty()) {
            const char* urls[] = {
                //"rtsp://admin:qwerty123@192.168.1.3:554/Streaming/channels/101",
                //"rtsp://admin:qwerty123@192.168.1.4:554/Streaming/channels/101",
                //"rtsp://admin:qwerty123@192.168.1.19:554/Streaming/channels/101",
                // "rtsp://admin:qwerty123@192.168.1.23:554/Streaming/channels/101"
            };
            // Only set default URL if within the hardcoded array bounds
            if (i < 3) {
                rs.rtspUrl = urls[i];
            }
            // For streams 4-8, leave empty (user must configure)
        }
    }

    qDebug() << "Settings loaded successfully";
}

void MainWindow::saveSettings()
{
    QSettings settings("MotionRecorder", "QtGStreamer");

    settings.setValue("autoStart", m_globalSettings.autoStart);

    // Save all configured streams (support up to 9 streams for 3x3 grid)
    int maxStreams = m_globalSettings.recorderSettings.size();
    for (int i = 0; i < maxStreams; ++i) {
        QString prefix = QString("stream%1/").arg(i);
        const RecorderSettings &rs = m_globalSettings.recorderSettings[i];

        // Camera Details
        settings.setValue(prefix + "cameraName", rs.cameraName);
        settings.setValue(prefix + "rtspUrl", rs.rtspUrl);
        settings.setValue(prefix + "outputPath", rs.outputPath);

        // Live Stream Settings
        settings.setValue(prefix + "liveStreamWidth", rs.liveStreamWidth);
        settings.setValue(prefix + "liveStreamHeight", rs.liveStreamHeight);
        settings.setValue(prefix + "liveStreamFps", rs.liveStreamFps);

        // Recording Settings
        settings.setValue(prefix + "bitrate", rs.bitrateKbps);
        settings.setValue(prefix + "normalFps", rs.normalFps);
        settings.setValue(prefix + "lowFps", rs.lowFps);
        settings.setValue(prefix + "motionThreshold", rs.motionThreshold);
        settings.setValue(prefix + "width", rs.width);
        settings.setValue(prefix + "height", rs.height);
        settings.setValue(prefix + "motionFramesToStart", rs.motionFramesToStart);
        settings.setValue(prefix + "noMotionFramesToStop", rs.noMotionFramesToStop);
        settings.setValue(prefix + "enabled", rs.enabled);
    }

    qDebug() << "Settings saved successfully";
}

void MainWindow::startMotionRecorder(int index)
{
    if (index < 0 || index >= m_recorderWorkers.size()) return;
    if (!m_recorderWorkers[index]) return;

    // Ensure Recordings directory exists before starting recording
    QString recordingsDir = "/workspace/Recordings";
    QDir dir;
    if (!dir.exists(recordingsDir)) {
        if (dir.mkpath(recordingsDir)) {
            qDebug() << "Created Recordings directory at:" << recordingsDir;
        } else {
            qDebug() << "ERROR: Failed to create Recordings directory at:" << recordingsDir;
        }
    }

    // Generate new filename with timestamp for this recording session
    QString cameraName = m_globalSettings.recorderSettings[index].cameraName;
    QString sanitizedName = cameraName;
    // Replace invalid filename characters with underscore
    for (int j = 0; j < sanitizedName.length(); ++j) {
        QChar c = sanitizedName[j];
        if (!c.isLetterOrNumber() && c != '_' && c != '-') {
            sanitizedName[j] = '_';
        }
    }
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    QString baseName = sanitizedName.isEmpty() ? QString("Camera_%1").arg(index+1) : sanitizedName;
    QString filename = QString("%1_%2.mkv").arg(baseName).arg(timestamp);
    m_globalSettings.recorderSettings[index].outputPath = recordingsDir + "/" + filename;

    qDebug() << "Starting recording to:" << m_globalSettings.recorderSettings[index].outputPath;

    // Call startRecording on worker thread using QMetaObject::invokeMethod
    QMetaObject::invokeMethod(m_recorderWorkers[index], "startRecording",
                              Qt::QueuedConnection,
                              Q_ARG(RecorderSettings, m_globalSettings.recorderSettings[index]));

    qDebug() << "Started motion recorder" << index;
}

void MainWindow::stopMotionRecorder(int index)
{
    if (index < 0 || index >= m_recorderWorkers.size()) return;
    if (!m_recorderWorkers[index]) return;

    // Call stopRecording on worker thread
    QMetaObject::invokeMethod(m_recorderWorkers[index], "stopRecording", Qt::QueuedConnection);

    qDebug() << "Stopped motion recorder" << index;
}

// ============ SLOTS ============

// OLD_UI: void MainWindow::onStreamCheckboxToggled(int streamIndex, bool checked)
// OLD_UI: {
// OLD_UI:     if (streamIndex < 0 || streamIndex >= pipelines.size()) return;
// OLD_UI: 
// OLD_UI:     if (checked) {
// OLD_UI:         // Stream is already started in showEvent, just set to PLAYING if paused
// OLD_UI:         if (pipelines[streamIndex]) {
// OLD_UI:             gst_element_set_state(pipelines[streamIndex], GST_STATE_PLAYING);
// OLD_UI:         }
// OLD_UI:     } else {
// OLD_UI:         // Hide stream but don't destroy pipeline (saves resources)
// OLD_UI:         if (pipelines[streamIndex]) {
// OLD_UI:             gst_element_set_state(pipelines[streamIndex], GST_STATE_PAUSED);
// OLD_UI:         }
// OLD_UI:         if (streamIndex < videoLabels.size() && videoLabels[streamIndex]) {
// OLD_UI:             videoLabels[streamIndex]->clear();
// OLD_UI:             videoLabels[streamIndex]->setText("Stream hidden (recording continues)");
// OLD_UI:         }
// OLD_UI:     }
// OLD_UI: }
// OLD_UI: 
// OLD_UI: void MainWindow::onRecordCheckboxToggled(int streamIndex, bool checked)
// OLD_UI: {
// OLD_UI:     if (streamIndex < 0 || streamIndex >= m_globalSettings.recorderSettings.size()) return;
// OLD_UI: 
// OLD_UI:     m_globalSettings.recorderSettings[streamIndex].enabled = checked;
// OLD_UI: 
// OLD_UI:     if (checked) {
// OLD_UI:         startMotionRecorder(streamIndex);
// OLD_UI:     } else {
// OLD_UI:         stopMotionRecorder(streamIndex);
// OLD_UI:     }
// OLD_UI: 
// OLD_UI:     saveSettings();
// OLD_UI: }
// OLD_UI: 
// OLD_UI: void MainWindow::onSettingsButtonClicked()
// OLD_UI: {
// OLD_UI:     SettingsDialog dialog(this);
// OLD_UI:     dialog.setSettings(m_globalSettings);
// OLD_UI: 
// OLD_UI:     if (dialog.exec() == QDialog::Accepted) {
// OLD_UI:         m_globalSettings = dialog.getSettings();
// OLD_UI:         saveSettings();
// OLD_UI: 
// OLD_UI:         // Update record checkboxes
// OLD_UI:         for (int i = 0; i < 4; ++i) {
// OLD_UI:             m_recordCheckboxes[i]->setChecked(m_globalSettings.recorderSettings[i].enabled);
// OLD_UI:         }
// OLD_UI: 
// OLD_UI:         // Restart active recorders with new settings
// OLD_UI:         for (int i = 0; i < 4; ++i) {
// OLD_UI:             if (m_globalSettings.recorderSettings[i].enabled) {
// OLD_UI:                 // Stop and restart to apply new settings
// OLD_UI:                 stopMotionRecorder(i);
// OLD_UI:                 QTimer::singleShot(500, this, [this, i]() {
// OLD_UI:                     startMotionRecorder(i);
// OLD_UI:                 });
// OLD_UI:             }
// OLD_UI:         }
// OLD_UI: 
// OLD_UI:         QMessageBox::information(this, "Settings Applied",
// OLD_UI:                                  "Settings have been saved and active recorders restarted.");
// OLD_UI:     }
// OLD_UI: }
// OLD_UI:

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    static bool started = false;
    if (started) return;
    started = true;

    // Stagger stream starts to reduce initial load
    QTimer::singleShot(100, this, [this]() {
        startStream(0, "rtsp://admin:qwerty&123@192.168.1.3:554/Streaming/channels/101");
    });
    QTimer::singleShot(600, this, [this]() {
        startStream(1, "rtsp://admin:qwerty123@192.168.1.4:554/Streaming/channels/101");
    });
    QTimer::singleShot(1100, this, [this]() {
        startStream(2, "rtsp://admin:qwerty123@192.168.1.23:554/Streaming/channels/101");
    });
    QTimer::singleShot(1600, this, [this]() {
         startStream(3, "rtsp://admin:qwerty123@192.168.1.12:554/stream2");
     });
}

// ==================== NEW UI SETUP METHODS ====================

void MainWindow::setupUI()
{
    // Create navbar widget
    QWidget *navbar = new QWidget(this);
    navbar->setFixedHeight(60);
    navbar->setStyleSheet(
        "QWidget {"
        "  background: white;"
        "  border-bottom: 2px solid #e5e7eb;"
        "}"
    );

    QHBoxLayout *navbarLayout = new QHBoxLayout(navbar);
    navbarLayout->setContentsMargins(15, 5, 15, 5);
    navbarLayout->setSpacing(10);

    // LEFT: Logo (full navbar height)
    QLabel *logoLabel = new QLabel(navbar);
    QPixmap logoPix("/workspace/qt/build/logo3.jpeg");
    if (!logoPix.isNull()) {
        // Scale to full navbar height minus padding
        logoLabel->setPixmap(logoPix.scaled(200, 55, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        logoLabel->setScaledContents(false);
    } else {
        // Fallback if logo not found
        logoLabel->setText("📹");
        logoLabel->setStyleSheet("font-size: 40px;");
        qDebug() << "Logo image not found at /workspace/qt/build/logo3.jpeg";
    }
    navbarLayout->addWidget(logoLabel);

    navbarLayout->addStretch();

    // RIGHT: Settings button (3-dot menu - no circle)
    m_mainSettingsButton = new QPushButton("⋮", navbar);
    m_mainSettingsButton->setFixedSize(44, 44);
    m_mainSettingsButton->setCursor(Qt::PointingHandCursor);
    m_mainSettingsButton->setStyleSheet(
        "QPushButton {"
        "  background: transparent;"
        "  color: #1f2937;"
        "  border: none;"
        "  font-size: 28px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  color: #000000;"
        "}"
        "QPushButton:pressed {"
        "  color: #4b5563;"
        "}"
    );
    connect(m_mainSettingsButton, &QPushButton::clicked, this, &MainWindow::onMainSettingsClicked);
    navbarLayout->addWidget(m_mainSettingsButton);

    navbar->setLayout(navbarLayout);

    // Store navbar to add to main layout later
    m_navbar = navbar;
}

void MainWindow::setupVideoGrid()
{
    // Create main container widget
    QWidget *mainContainer = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(mainContainer);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Add navbar at top
    mainLayout->addWidget(m_navbar);

    // Create central widget with grid layout for videos
    m_centralWidget = new QWidget(mainContainer);
    m_videoGrid = new QGridLayout(m_centralWidget);
    m_videoGrid->setSpacing(2);
    m_videoGrid->setContentsMargins(0, 0, 0, 0);

    // Create video containers with overlays for initial 4 streams
    for (int i = 0; i < videoContainers.size(); ++i) {
        // Container widget (holds video label + overlay)
        videoContainers[i] = new QWidget(m_centralWidget);
        videoContainers[i]->setMinimumSize(320, 240);
        videoContainers[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        QStackedLayout *stackLayout = new QStackedLayout(videoContainers[i]);
        stackLayout->setStackingMode(QStackedLayout::StackAll);

        // Video label (background layer)
        videoLabels[i] = new QLabel(videoContainers[i]);
        videoLabels[i]->setAlignment(Qt::AlignCenter);
        videoLabels[i]->setStyleSheet("background-color: black; color: white;");
        videoLabels[i]->setText(QString("Camera %1\nWaiting for stream...").arg(i + 1));
        videoLabels[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        stackLayout->addWidget(videoLabels[i]);

        // Overlay controls (foreground layer)
        streamOverlays[i] = new StreamOverlay(i, videoContainers[i]);
        connect(streamOverlays[i], &StreamOverlay::liveStreamToggled,
                this, &MainWindow::onLiveStreamToggled);
        connect(streamOverlays[i], &StreamOverlay::motionRecordingToggled,
                this, &MainWindow::onMotionRecordingToggled);
        connect(streamOverlays[i], &StreamOverlay::settingsClicked,
                this, &MainWindow::onStreamSettingsClicked);
        connect(streamOverlays[i], &StreamOverlay::bottomSettingsClicked,
                this, &MainWindow::onBottomStreamSettingsClicked);
        connect(streamOverlays[i], &StreamOverlay::fullscreenClicked,
                this, &MainWindow::onFullscreenClicked);
        stackLayout->addWidget(streamOverlays[i]);
        streamOverlays[i]->show();
        streamOverlays[i]->raise();

        // Add to grid (2x2 layout initially)
        m_videoGrid->addWidget(videoContainers[i], i / 2, i % 2);
        videoContainers[i]->show();
    }

    // Set equal stretch for initial 2x2 grid
    m_videoGrid->setRowStretch(0, 1);
    m_videoGrid->setRowStretch(1, 1);
    m_videoGrid->setColumnStretch(0, 1);
    m_videoGrid->setColumnStretch(1, 1);

    // Add video grid to main layout
    mainLayout->addWidget(m_centralWidget, 1);  // Stretch factor 1

    // Set main container as central widget
    setCentralWidget(mainContainer);
}

// ==================== NEW SLOT IMPLEMENTATIONS ====================

void MainWindow::onLiveStreamToggled(int streamIndex, bool enabled)
{
    qDebug() << "Live stream" << streamIndex << "toggled:" << enabled;

    if (streamIndex < 0 || streamIndex >= videoLabels.size()) return;

    // Update live stream state
    if (streamIndex < liveStreamActive.size()) {
        liveStreamActive[streamIndex] = enabled;
    }

    // Update global map for appsink callback
    if (videoLabels[streamIndex]) {
        g_liveStreamEnabled[videoLabels[streamIndex]] = enabled;
    }

    // TEE ARCHITECTURE: Pipeline always runs for recording
    // Live toggle just controls display visibility
    if (enabled) {
        // Show live stream - video widget will receive frames from appsink
        if (videoLabels[streamIndex]) {
            videoLabels[streamIndex]->clear();
            videoLabels[streamIndex]->show();
            qDebug() << "Live stream" << streamIndex << "display enabled";
        }
    } else {
        // Hide live stream - pipeline keeps running for recording
        if (videoLabels[streamIndex]) {
            videoLabels[streamIndex]->clear();
            videoLabels[streamIndex]->setText(QString("Camera %1\nLive Stream OFF").arg(streamIndex + 1));
            qDebug() << "Live stream" << streamIndex << "display disabled (pipeline still running)";
        }
    }
}

void MainWindow::onMotionRecordingToggled(int streamIndex, bool enabled)
{
    qDebug() << "Motion recording" << streamIndex << "toggled:" << enabled;

    if (streamIndex < 0 || streamIndex >= recordingValves.size()) return;
    if (!recordingValves[streamIndex] || !recordingMuxers[streamIndex]) {
        qWarning() << "Recording elements not available for stream" << streamIndex;
        return;
    }

    m_globalSettings.recorderSettings[streamIndex].enabled = enabled;
    recordingActive[streamIndex] = enabled;

    // Update overlay button state
    if (streamIndex < streamOverlays.size() && streamOverlays[streamIndex]) {
        streamOverlays[streamIndex]->setMotionRecordingEnabled(enabled);
    }

    if (enabled) {
        qDebug() << "✅ STARTING RECORDING - Creating new file for stream" << streamIndex;

        // Generate NEW timestamped filename for this recording session
        QString baseDir = (streamIndex < m_globalSettings.recorderSettings.size() &&
                          !m_globalSettings.recorderSettings[streamIndex].outputPath.isEmpty()) ?
                          QFileInfo(m_globalSettings.recorderSettings[streamIndex].outputPath).absolutePath() :
                          QString("/workspace/Recordings");

        QDir().mkpath(baseDir);  // Create directory if needed

        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString outputPath = QString("%1/stream_%2_%3.mkv").arg(baseDir).arg(streamIndex).arg(timestamp);

        // Create filesink element
        QString name = QString("stream%1_").arg(streamIndex);
        GstElement *filesink = gst_element_factory_make("filesink", (name + "filesink").toUtf8().constData());
        if (!filesink) {
            qWarning() << "Failed to create filesink for stream" << streamIndex;
            return;
        }

        // Configure filesink
        g_object_set(filesink,
                     "location", outputPath.toUtf8().constData(),
                     "sync", FALSE,
                     "async", FALSE,
                     nullptr);

        // Remove fakesink and link new filesink
        if (recordingFakesinks[streamIndex]) {
            gst_element_set_state(recordingFakesinks[streamIndex], GST_STATE_NULL);
            gst_element_unlink(recordingMuxers[streamIndex], recordingFakesinks[streamIndex]);
            gst_bin_remove(GST_BIN(pipelines[streamIndex]), recordingFakesinks[streamIndex]);
            recordingFakesinks[streamIndex] = nullptr;
        }

        // Add filesink to pipeline
        gst_bin_add(GST_BIN(pipelines[streamIndex]), filesink);

        // Link muxer to filesink
        if (!gst_element_link(recordingMuxers[streamIndex], filesink)) {
            qWarning() << "Failed to link muxer to filesink for stream" << streamIndex;
            gst_bin_remove(GST_BIN(pipelines[streamIndex]), filesink);
            gst_object_unref(filesink);
            return;
        }

        // Sync filesink state with pipeline
        gst_element_sync_state_with_parent(filesink);
        qDebug() << "Filesink linked and synced to PLAYING state - ready to record";

        // Store filesink reference
        recordingFilesinks[streamIndex] = filesink;

        qDebug() << "New recording file created:" << outputPath;

        // RESET PTS and frame state (safety double-check - already reset during encoder recreation)
        g_lastPts[streamIndex] = GST_CLOCK_TIME_NONE;
        g_accumulatedPts[streamIndex] = 0;
        g_isLowFpsMode[streamIndex] = true;   // START in LOW FPS mode (1 FPS, 10x playback)
        g_frameDropCounter[streamIndex] = -20; // Keep first 20 frames to ensure keyframe is captured

        qDebug() << "🎬 Starting recording in LOW FPS mode (keeping first 20 frames for keyframe)";

        // OPEN VALVE FIRST - Allow data to flow to recording branch
        g_object_set(recordingValves[streamIndex], "drop", FALSE, nullptr);
        qDebug() << "✅ Valve opened - data flowing to recording branch";

        // Small delay to ensure valve is fully open
        QThread::msleep(50);

        // NOW force keyframe AFTER valve is open (so it actually reaches the encoder!)
        if (recordingEncoders[streamIndex]) {
            forceKeyframe(recordingEncoders[streamIndex]);
            qDebug() << "🔑 Keyframe forced - encoder will generate IDR frame with SPS/PPS headers";
        }

        qDebug() << "Recording started for stream" << streamIndex << "-> " << outputPath;
    } else {
        qDebug() << "❌ STOPPING RECORDING for stream" << streamIndex;

        // CLOSE VALVE - Block data flow (drops all buffers)
        g_object_set(recordingValves[streamIndex], "drop", TRUE, nullptr);

        // Small delay to ensure buffers are flushed
        QThread::msleep(200);

        if (!recordingFilesinks[streamIndex]) {
            qWarning() << "No filesink to stop for stream" << streamIndex;
            saveSettings();
            return;
        }

        // Get file path before removing
        gchar *location = nullptr;
        g_object_get(recordingFilesinks[streamIndex], "location", &location, nullptr);
        QString filePath = location ? QString::fromUtf8(location) : QString();
        if (location) g_free(location);

        // DESTROY and RECREATE encoder + parser + muxer for fresh codec headers
        // This ensures SPS/PPS headers are sent for each recording

        // Set all recording elements to NULL
        gst_element_set_state(recordingFilesinks[streamIndex], GST_STATE_NULL);
        gst_element_set_state(recordingMuxers[streamIndex], GST_STATE_NULL);
        gst_element_set_state(recordingParsers[streamIndex], GST_STATE_NULL);
        gst_element_set_state(recordingEncoders[streamIndex], GST_STATE_NULL);
        if (recordingFakesinks[streamIndex]) {
            gst_element_set_state(recordingFakesinks[streamIndex], GST_STATE_NULL);
        }

        // Unlink: capsfilter → encoder → parser → muxer → filesink
        gst_element_unlink(recordingCapsfilters[streamIndex], recordingEncoders[streamIndex]);
        gst_element_unlink(recordingEncoders[streamIndex], recordingParsers[streamIndex]);
        gst_element_unlink(recordingParsers[streamIndex], recordingMuxers[streamIndex]);
        gst_element_unlink(recordingMuxers[streamIndex], recordingFilesinks[streamIndex]);

        // Remove encoder, parser, muxer, filesink from pipeline
        gst_bin_remove(GST_BIN(pipelines[streamIndex]), recordingEncoders[streamIndex]);
        gst_bin_remove(GST_BIN(pipelines[streamIndex]), recordingParsers[streamIndex]);
        gst_bin_remove(GST_BIN(pipelines[streamIndex]), recordingMuxers[streamIndex]);
        gst_bin_remove(GST_BIN(pipelines[streamIndex]), recordingFilesinks[streamIndex]);
        if (recordingFakesinks[streamIndex]) {
            gst_bin_remove(GST_BIN(pipelines[streamIndex]), recordingFakesinks[streamIndex]);
        }

        // Clear old references
        recordingEncoders[streamIndex] = nullptr;
        recordingParsers[streamIndex] = nullptr;
        recordingMuxers[streamIndex] = nullptr;
        recordingFilesinks[streamIndex] = nullptr;
        recordingFakesinks[streamIndex] = nullptr;

        // CREATE NEW encoder, parser, muxer, fakesink for next recording
        QString name = QString("stream%1_").arg(streamIndex);
        GstElement *newEncoder = gst_element_factory_make("nvh265enc", (name + "encoder").toUtf8().constData());
        GstElement *newParser = gst_element_factory_make("h265parse", (name + "parser").toUtf8().constData());
        GstElement *newMuxer = gst_element_factory_make("matroskamux", (name + "muxer").toUtf8().constData());
        GstElement *newFakesink = gst_element_factory_make("fakesink", (name + "fakesink").toUtf8().constData());

        if (!newEncoder || !newParser || !newMuxer || !newFakesink) {
            qWarning() << "Failed to create new recording elements for stream" << streamIndex;
            return;
        }

        // Configure encoder with same settings
        int recordBitrate = (streamIndex < m_globalSettings.recorderSettings.size()) ?
                            m_globalSettings.recorderSettings[streamIndex].bitrateKbps : 800;
        g_object_set(newEncoder,
                     "bitrate", recordBitrate,
                     "preset", 2,  // Low latency HP
                     "gop-size", 30,
                     "zerolatency", TRUE,
                     nullptr);

        // Configure fakesink
        g_object_set(newFakesink, "sync", FALSE, "async", FALSE, nullptr);

        // Add all new elements to pipeline
        gst_bin_add_many(GST_BIN(pipelines[streamIndex]), newEncoder, newParser, newMuxer, newFakesink, nullptr);

        // Link: capsfilter → new encoder → new parser → new muxer → new fakesink
        if (!gst_element_link(recordingCapsfilters[streamIndex], newEncoder)) {
            qWarning() << "Failed to link capsfilter to new encoder";
            return;
        }
        if (!gst_element_link(newEncoder, newParser)) {
            qWarning() << "Failed to link new encoder to parser";
            return;
        }
        if (!gst_element_link(newParser, newMuxer)) {
            qWarning() << "Failed to link new parser to muxer";
            return;
        }
        if (!gst_element_link(newMuxer, newFakesink)) {
            qWarning() << "Failed to link new muxer to fakesink";
            return;
        }

        // RESET all state variables for this stream BEFORE attaching pad probe
        // This ensures clean state for next recording (PTS starts from 0)
        g_lastPts[streamIndex] = GST_CLOCK_TIME_NONE;
        g_accumulatedPts[streamIndex] = 0;
        g_frameDropCounter[streamIndex] = 0;
        g_isLowFpsMode[streamIndex] = false;
        g_motionFrameCount[streamIndex] = 0;
        if (g_previousFrames.contains(streamIndex)) {
            g_previousFrames[streamIndex].clear();
        }
        qDebug() << "🔄 Reset all state variables for stream" << streamIndex;

        // CRITICAL: Reattach pad probe to new encoder for frame dropping and timestamp manipulation
        GstPad *encoderSinkPad = gst_element_get_static_pad(newEncoder, "sink");
        if (encoderSinkPad) {
            gst_pad_add_probe(encoderSinkPad, GST_PAD_PROBE_TYPE_BUFFER,
                             (GstPadProbeCallback) frameDropAndTimestampProbe,
                             GINT_TO_POINTER(streamIndex), nullptr);
            gst_object_unref(encoderSinkPad);
            qDebug() << "✅ Pad probe reattached to new encoder for stream" << streamIndex;
        }

        // Sync all states to PLAYING
        gst_element_sync_state_with_parent(newEncoder);
        gst_element_sync_state_with_parent(newParser);
        gst_element_sync_state_with_parent(newMuxer);
        gst_element_sync_state_with_parent(newFakesink);

        // Store new references
        recordingEncoders[streamIndex] = newEncoder;
        recordingParsers[streamIndex] = newParser;
        recordingMuxers[streamIndex] = newMuxer;
        recordingFakesinks[streamIndex] = newFakesink;

        // Update global pointer for pad probe (motion detection needs encoder reference)
        g_recordingEncoders = &recordingEncoders;

        qDebug() << "✅ Recreated encoder+parser+muxer with pad probe - ready for next recording";

        // Check file size and delete if empty
        if (!filePath.isEmpty()) {
            QFileInfo fileInfo(filePath);
            if (fileInfo.exists()) {
                qint64 fileSize = fileInfo.size();
                qDebug() << "Recording file size:" << fileSize << "bytes";

                // Delete if file is empty or less than 5KB (header only, no real frames)
                if (fileSize < 5120) {
                    qDebug() << "⚠️ Deleting empty/incomplete recording:" << filePath;
                    QFile::remove(filePath);
                } else {
                    qDebug() << "✅ Recording saved:" << filePath << "(" << (fileSize/1024) << "KB)";
                }
            }
        }

        qDebug() << "Recording stopped for stream" << streamIndex << "- Ready for next recording";
    }

    saveSettings();
}

void MainWindow::onStreamSettingsClicked(int streamIndex)
{
    qDebug() << "Stream settings clicked for stream" << streamIndex;

    if (streamIndex < 0 || streamIndex >= m_globalSettings.recorderSettings.size()) return;

    // Get current settings
    const RecorderSettings &current = m_globalSettings.recorderSettings[streamIndex];
    StreamRecordingSettingsDialog::Settings settings;

    // Camera Details
    settings.cameraName = current.cameraName;
    settings.rtspUrl = current.rtspUrl;

    // Live Stream Settings
    settings.liveStreamWidth = current.liveStreamWidth;
    settings.liveStreamHeight = current.liveStreamHeight;
    settings.liveStreamFps = current.liveStreamFps;

    // Recording Settings
    settings.recordingWidth = current.width;
    settings.recordingHeight = current.height;
    settings.withMotionFps = current.normalFps;
    settings.withoutMotionFps = current.lowFps;
    settings.bitrateKbps = current.bitrateKbps;
    settings.motionThreshold = current.motionThreshold;
    settings.pixelSensitivity = current.pixelSensitivity;

    // Show dialog
    StreamRecordingSettingsDialog dialog(streamIndex, settings, this);
    if (dialog.exec() == QDialog::Accepted) {
        // Apply new settings
        StreamRecordingSettingsDialog::Settings newSettings = dialog.getSettings();

        // Check if RTSP URL or live stream settings changed
        bool urlChanged = (m_globalSettings.recorderSettings[streamIndex].rtspUrl != newSettings.rtspUrl);
        bool liveStreamChanged = (m_globalSettings.recorderSettings[streamIndex].liveStreamWidth != newSettings.liveStreamWidth ||
                                  m_globalSettings.recorderSettings[streamIndex].liveStreamHeight != newSettings.liveStreamHeight ||
                                  m_globalSettings.recorderSettings[streamIndex].liveStreamFps != newSettings.liveStreamFps);

        // Camera Details
        m_globalSettings.recorderSettings[streamIndex].cameraName = newSettings.cameraName;
        m_globalSettings.recorderSettings[streamIndex].rtspUrl = newSettings.rtspUrl;

        // Update output path based on camera name
        QString sanitizedName = newSettings.cameraName;
        // Replace invalid filename characters with underscore
        for (int j = 0; j < sanitizedName.length(); ++j) {
            QChar c = sanitizedName[j];
            if (!c.isLetterOrNumber() && c != '_' && c != '-') {
                sanitizedName[j] = '_';
            }
        }

        // Create Recordings directory if it doesn't exist
        QString recordingsDir = "/workspace/Recordings";
        QDir dir;
        if (!dir.exists(recordingsDir)) {
            dir.mkpath(recordingsDir);
            qDebug() << "Created Recordings directory at:" << recordingsDir;
        }

        // Generate filename with timestamp (unique for each recording)
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
        QString baseName = sanitizedName.isEmpty() ? QString("Camera_%1").arg(streamIndex+1) : sanitizedName;
        QString filename = QString("%1_%2.mkv").arg(baseName).arg(timestamp);
        m_globalSettings.recorderSettings[streamIndex].outputPath = recordingsDir + "/" + filename;

        // Live Stream Settings
        m_globalSettings.recorderSettings[streamIndex].liveStreamWidth = newSettings.liveStreamWidth;
        m_globalSettings.recorderSettings[streamIndex].liveStreamHeight = newSettings.liveStreamHeight;
        m_globalSettings.recorderSettings[streamIndex].liveStreamFps = newSettings.liveStreamFps;

        // Recording Settings
        m_globalSettings.recorderSettings[streamIndex].normalFps = newSettings.withMotionFps;
        m_globalSettings.recorderSettings[streamIndex].lowFps = newSettings.withoutMotionFps;
        m_globalSettings.recorderSettings[streamIndex].bitrateKbps = newSettings.bitrateKbps;
        m_globalSettings.recorderSettings[streamIndex].width = newSettings.recordingWidth;
        m_globalSettings.recorderSettings[streamIndex].height = newSettings.recordingHeight;
        m_globalSettings.recorderSettings[streamIndex].motionThreshold = newSettings.motionThreshold;
        m_globalSettings.recorderSettings[streamIndex].pixelSensitivity = newSettings.pixelSensitivity;

        saveSettings();

        // If URL or live stream settings changed, need to restart stream
        if (urlChanged || liveStreamChanged) {
            qDebug() << "Stream settings changed for stream" << streamIndex << "- restarting";

            // DISABLED: Old worker system - recording now handled by TEE pipeline VALVE
            // Stop recorder if active
            // if (m_globalSettings.recorderSettings[streamIndex].enabled) {
            //     stopMotionRecorder(streamIndex);
            // }

            // Restart stream with new URL/settings
            stopStream(streamIndex);
            QTimer::singleShot(500, this, [this, streamIndex, newSettings]() {
                startStream(streamIndex, newSettings.rtspUrl.toUtf8().constData());
            });

            // DISABLED: Old worker system - recording now handled by TEE pipeline VALVE
            // Restart recorder with new settings if it was active
            // if (m_globalSettings.recorderSettings[streamIndex].enabled) {
            //     QTimer::singleShot(1000, this, [this, streamIndex]() {
            //         startMotionRecorder(streamIndex);
            //     });
            // }

            QString message = urlChanged ?
                QString("Camera %1 switched to new URL: %2").arg(streamIndex + 1).arg(newSettings.cameraName) :
                QString("Live stream settings updated for %1").arg(newSettings.cameraName);
            QMessageBox::information(this, "Settings Updated", message);
        } else {
            // DISABLED: Old worker system - recording now handled by TEE pipeline VALVE
            // Only recording settings changed, just restart recorder if active
            // if (m_globalSettings.recorderSettings[streamIndex].enabled) {
            //     stopMotionRecorder(streamIndex);
            //     QTimer::singleShot(500, this, [this, streamIndex]() {
            //         startMotionRecorder(streamIndex);
            //     });
            // }

            QMessageBox::information(this, "Settings Updated",
                                     QString("Recording settings updated for %1").arg(newSettings.cameraName));
        }
    }
}

void MainWindow::onBottomStreamSettingsClicked(int streamIndex)
{
    qDebug() << "Bottom settings (gear icon) clicked for stream" << streamIndex;

    // Open StreamControlWindow with navbar (Playback & Settings)
    StreamControlWindow *controlWindow = new StreamControlWindow(streamIndex, this);
    controlWindow->exec();
    delete controlWindow;
}

void MainWindow::onFullscreenClicked(int streamIndex)
{
    qDebug() << "Fullscreen clicked for stream" << streamIndex;

    if (m_isFullscreen) {
        exitFullscreen();
    } else {
        enterFullscreen(streamIndex);
    }
}

void MainWindow::onMainSettingsClicked()
{
    qDebug() << "Main settings button clicked";

    GridSettingsDialog dialog(m_currentGridMode, this);
    if (dialog.exec() == QDialog::Accepted) {
        GridSettingsDialog::GridMode newMode = dialog.getSelectedMode();
        if (newMode != m_currentGridMode) {
            updateGridLayout(newMode);
            m_currentGridMode = newMode;
        }
    }
}

// ==================== GRID SWITCHING ====================

void MainWindow::updateGridLayout(GridSettingsDialog::GridMode mode)
{
    qDebug() << "Switching grid layout to mode:" << (int)mode;

    // Clear the grid layout completely
    QLayoutItem *item;
    while ((item = m_videoGrid->takeAt(0)) != nullptr) {
        // Don't delete widgets, just remove from layout
        delete item;
    }

    // Reset all row and column stretches to 0
    for (int i = 0; i < 10; ++i) {
        m_videoGrid->setRowStretch(i, 0);
        m_videoGrid->setColumnStretch(i, 0);
    }

    int requiredStreams = (mode == GridSettingsDialog::Grid2x2) ? 4 : 9;
    int currentStreams = videoContainers.size();

    // Expand vectors if switching to 3x3
    if (requiredStreams > currentStreams) {
        int oldSize = currentStreams;
        videoContainers.resize(requiredStreams);
        videoLabels.resize(requiredStreams);
        streamOverlays.resize(requiredStreams);
        pipelines.resize(requiredStreams);
        m_recorderThreads.resize(requiredStreams);
        m_recorderWorkers.resize(requiredStreams);

        // Create new video containers for additional streams
        for (int i = oldSize; i < requiredStreams; ++i) {
            // Container
            videoContainers[i] = new QWidget(m_centralWidget);
            videoContainers[i]->setMinimumSize(320, 240);
            videoContainers[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

            QStackedLayout *stackLayout = new QStackedLayout(videoContainers[i]);
            stackLayout->setStackingMode(QStackedLayout::StackAll);

            // Video label
            videoLabels[i] = new QLabel(videoContainers[i]);
            videoLabels[i]->setAlignment(Qt::AlignCenter);
            videoLabels[i]->setStyleSheet("background-color: black; color: white;");
            videoLabels[i]->setText(QString("Camera %1\nNot configured").arg(i + 1));
            videoLabels[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            stackLayout->addWidget(videoLabels[i]);

            // Overlay
            streamOverlays[i] = new StreamOverlay(i, videoContainers[i]);
            connect(streamOverlays[i], &StreamOverlay::liveStreamToggled,
                    this, &MainWindow::onLiveStreamToggled);
            connect(streamOverlays[i], &StreamOverlay::motionRecordingToggled,
                    this, &MainWindow::onMotionRecordingToggled);
            connect(streamOverlays[i], &StreamOverlay::settingsClicked,
                    this, &MainWindow::onStreamSettingsClicked);
            connect(streamOverlays[i], &StreamOverlay::bottomSettingsClicked,
                    this, &MainWindow::onBottomStreamSettingsClicked);
            connect(streamOverlays[i], &StreamOverlay::fullscreenClicked,
                    this, &MainWindow::onFullscreenClicked);
            stackLayout->addWidget(streamOverlays[i]);
            streamOverlays[i]->show();
            streamOverlays[i]->raise();

            // Initialize workers for new streams
            m_recorderThreads[i] = new QThread(this);
            m_recorderWorkers[i] = new MotionRecorderWorker(i);
            m_recorderWorkers[i]->moveToThread(m_recorderThreads[i]);
            connect(m_recorderWorkers[i], &MotionRecorderWorker::statusUpdate,
                    this, [this, i](const QString &status) {
                onRecorderStatusUpdate(i, status);
            });
            connect(m_recorderWorkers[i], &MotionRecorderWorker::motionDetected,
                    this, [this, i](bool hasMotion) {
                onMotionDetected(i, hasMotion);
            });
            connect(m_recorderWorkers[i], &MotionRecorderWorker::errorOccurred,
                    this, [this, i](const QString &error) {
                onRecorderError(i, error);
            });
            m_recorderThreads[i]->start();
        }

        // Expand settings
        while (m_globalSettings.recorderSettings.size() < requiredStreams) {
            RecorderSettings rs;
            rs.enabled = false;
            rs.rtspUrl = "";

            // Default path: /workspace/Recordings
            rs.outputPath = "/workspace/Recordings/" + QString("motion_stream_%1.mkv").arg(m_globalSettings.recorderSettings.size());
            rs.bitrateKbps = 250;
            rs.normalFps = 5;
            rs.lowFps = 1;
            rs.motionThreshold = 1.0;
            rs.width = 640;
            rs.height = 360;
            rs.motionFramesToStart = 8;
            rs.noMotionFramesToStop = 50;
            m_globalSettings.recorderSettings.append(rs);
        }
    }

    // Add widgets back to grid in new layout
    if (mode == GridSettingsDialog::Grid2x2) {
        // 2x2 grid
        for (int i = 0; i < 4 && i < videoContainers.size(); ++i) {
            m_videoGrid->addWidget(videoContainers[i], i / 2, i % 2);
            videoContainers[i]->show();
        }
        // Hide extra containers if they exist
        for (int i = 4; i < videoContainers.size(); ++i) {
            videoContainers[i]->hide();
        }
        // Set equal stretch for 2x2 grid (2 rows, 2 columns)
        m_videoGrid->setRowStretch(0, 1);
        m_videoGrid->setRowStretch(1, 1);
        m_videoGrid->setColumnStretch(0, 1);
        m_videoGrid->setColumnStretch(1, 1);
    } else {
        // 3x3 grid
        for (int i = 0; i < 9 && i < videoContainers.size(); ++i) {
            m_videoGrid->addWidget(videoContainers[i], i / 3, i % 3);
            videoContainers[i]->show();
        }
        // Set equal stretch for 3x3 grid (3 rows, 3 columns)
        m_videoGrid->setRowStretch(0, 1);
        m_videoGrid->setRowStretch(1, 1);
        m_videoGrid->setRowStretch(2, 1);
        m_videoGrid->setColumnStretch(0, 1);
        m_videoGrid->setColumnStretch(1, 1);
        m_videoGrid->setColumnStretch(2, 1);
    }

    // Force layout update
    m_videoGrid->invalidate();
    m_videoGrid->activate();

    qDebug() << "Grid layout updated successfully";
}

// ==================== FULLSCREEN ====================

void MainWindow::enterFullscreen(int streamIndex)
{
    if (streamIndex < 0 || streamIndex >= videoContainers.size()) return;

    m_fullscreenStreamIndex = streamIndex;
    m_isFullscreen = true;

    // Remove all widgets from grid but keep their parent
    for (int i = 0; i < videoContainers.size(); ++i) {
        m_videoGrid->removeWidget(videoContainers[i]);
        if (i != streamIndex) {
            videoContainers[i]->hide();
        }
    }

    // Determine grid size for spanning
    int gridSize = (m_currentGridMode == GridSettingsDialog::Grid2x2) ? 2 : 3;

    // Add only the selected stream to fill entire grid (span all rows and columns)
    m_videoGrid->addWidget(videoContainers[streamIndex], 0, 0, gridSize, gridSize);
    videoContainers[streamIndex]->show();

    // Hide navbar
    m_navbar->hide();

    qDebug() << "Entered fullscreen for stream" << streamIndex;
}

void MainWindow::exitFullscreen()
{
    if (!m_isFullscreen) return;

    m_isFullscreen = false;

    // Show navbar
    m_navbar->show();

    // Restore grid layout
    updateGridLayout(m_currentGridMode);

    m_fullscreenStreamIndex = -1;

    qDebug() << "Exited fullscreen";
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_isFullscreen) {
        exitFullscreen();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

// ==================== RECORDER STATUS UPDATES ====================

void MainWindow::onRecorderStatusUpdate(int streamIndex, QString status)
{
    qDebug() << "Stream" << streamIndex << "recorder status:" << status;

    // Update overlay button state based on recording status
    if (streamIndex >= 0 && streamIndex < streamOverlays.size() && streamOverlays[streamIndex]) {
        if (status.contains("Recording started", Qt::CaseInsensitive) ||
            status.contains("Recording", Qt::CaseInsensitive)) {
            // Recording started successfully - turn button GREEN
            streamOverlays[streamIndex]->setMotionRecordingEnabled(true);
        } else if (status.contains("Stopped", Qt::CaseInsensitive) ||
                   status.contains("Failed", Qt::CaseInsensitive) ||
                   status.contains("Error", Qt::CaseInsensitive)) {
            // Recording stopped or failed - turn button GREY
            streamOverlays[streamIndex]->setMotionRecordingEnabled(false);
        }
    }
}

void MainWindow::onMotionDetected(int streamIndex, bool hasMotion)
{
    // Visual feedback for motion detection (optional)
    qDebug() << "Stream" << streamIndex << "motion:" << hasMotion;
}

void MainWindow::onRecorderError(int streamIndex, QString error)
{
    // Turn recording button GREY on error
    if (streamIndex >= 0 && streamIndex < streamOverlays.size() && streamOverlays[streamIndex]) {
        streamOverlays[streamIndex]->setMotionRecordingEnabled(false);
    }

    QMessageBox::warning(this, QString("Recording Error - Camera %1").arg(streamIndex + 1), error);
}

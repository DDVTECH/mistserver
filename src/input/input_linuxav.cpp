#include "input_linuxav.h"

#include <mist/config.h>
#include <mist/defines.h>
#include <mist/json.h>
#include <mist/stream.h>
#include <mist/util.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sstream>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#ifdef __linux__
#include <linux/videodev2.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/operation.h>
#include <pulse/subscribe.h>
#include <pulse/volume.h>
#include <pulse/xmalloc.h>
#endif

namespace Mist {

  LinuxAV::LinuxAV(Util::Config *cfg) : Input(cfg) {
    capa["name"] = "LinuxAV";
    capa["desc"] =
      "Enables combined audio and video input on Linux systems using V4L2 and PulseAudio";
    capa["source_match"] = "linuxav:*";
    capa["always_match"] = capa["source_match"];
    capa["priority"] = 9;
    capa["codecs"][0u][0u].append("PCM");
    capa["codecs"][0u][1u].append("JPEG");
    capa["codecs"][0u][1u].append("YUYV");
    capa["codecs"][0u][1u].append("UYVY");
    capa["codecs"][0u][1u].append("MJPG");
    capa["codecs"][0u][1u].append("NV12");
    capa["codecs"][0u][1u].append("BGR3");
    capa["codecs"][0u][1u].append("NV24");
    capa["codecs"][0u][1u].append("NV16");

#ifdef __linux__
    // Initialize video state (from V4L2)
    videoFd = -1;
    videoBuffer = nullptr;
    videoWidth = 0;
    videoHeight = 0;
    videoFpsDenominator = 0;
    videoFpsNumerator = 0;
    videoPixelFmt = 0;
    videoBufferType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    selectedVideoDevice = "";
    videoTrackIdx = INVALID_TRACK_ID;

    // Multi-planar buffer support
    numPlanes = 0;
    memset(videoPlanes, 0, sizeof(videoPlanes));

    // Audio parameters (auto-detected or configured)
    sampleRate = 48000;
    channels = 2;
    bufferSize = 1024;

    // State tracking
    isCapturing = false;
    hasVideo = true;
    hasAudio = true;
    frameCount = 0;
    startTimestamp = 0;

    // Initialize track indices
    videoTrackIdx = INVALID_TRACK_ID;
    audioTrackIdx = INVALID_TRACK_ID;
    selectedAudioDevice = "";

#ifdef WITH_PULSE
    // Initialize PulseAudio state
    mainloop = nullptr;
    context = nullptr;
    stream = nullptr;
    mainloopApi = nullptr;
    contextReady = false;
    streamReady = false;
    inputGain = 0.5f;
#else
    hasAudio = false;
    inputGain = 0.0f;
#endif
#endif

    // Video format option (from V4L2)
    JSON::Value option;
    option["arg"] = "string";
    option["long"] = "format";
    option["short"] = "F";
    option["help"] = "Video format like 'MJPG-1920x1080@30.00'. FPS is optional. Defaults to "
                     "highest resolution and FPS";
    option["value"].append("");
    config->addOption("format", option);

    capa["optional"]["format"]["name"] = "Video resolution, framerate and pixel format";
    capa["optional"]["format"]["help"] =
      "Video format like 'MJPG-1920x1080@30.00'. FPS is optional";
    capa["optional"]["format"]["option"] = "--format";
    capa["optional"]["format"]["short"] = "F";
    capa["optional"]["format"]["default"] = "";
    capa["optional"]["format"]["type"] = "string";

    // Audio options (from PulseAudio)
    option.null();
    option["arg"] = "integer";
    option["long"] = "samplerate";
    option["help"] = "Audio sample rate in Hz";
    option["value"].append(48000);
    config->addOption("samplerate", option);

    option.null();
    option["arg"] = "integer";
    option["long"] = "channels";
    option["help"] = "Number of audio channels";
    option["value"].append(2);
    config->addOption("channels", option);

    option.null();
    option["arg"] = "integer";
    option["long"] = "buffersize";
    option["help"] = "Audio buffer size";
    option["value"].append(1024);
    config->addOption("buffersize", option);

    option.null();
    option["arg"] = "double";
    option["long"] = "gain";
    option["help"] = "Audio input gain (0.0-2.0)";
    option["value"].append(0.5);
    config->addOption("gain", option);

    option.null();
    option["arg"] = "string";
    option["long"] = "video-device";
    option["help"] = "Video device path (e.g., /dev/video0)";
    option["value"].append("");
    config->addOption("video-device", option);

    option.null();
    option["arg"] = "string";
    option["long"] = "audio-device";
    option["help"] = "Audio device name";
    option["value"].append("");
    config->addOption("audio-device", option);

    // Enumeration support (from V4L2)
    capa["enum_static_prefix"] = "linuxav:";
    option.null();
    option["long"] = "enumerate";
    option["short"] = "e";
    option["help"] = "Output supported devices in JSON format, then exit";
    option["value"].append("");
    config->addOption("enumerate", option);

    capa["dynamic_capa"] = true;
    option.null();
    option["long"] = "getcapa";
    option["arg"] = "string";
    option["short"] = "q";
    option["help"] = "Output device capabilities for given device in JSON format, then exit";
    option["value"].append("");
    config->addOption("getcapa", option);
  }

  LinuxAV::~LinuxAV() {
    closeStreamSource();
  }

  bool LinuxAV::checkArguments() {
#ifndef __linux__
    FAIL_MSG("Combined A/V input is only supported on Linux systems");
    return false;
#else
    // Parse input string to determine video and audio devices
    std::string input = config->getString("input");

    // Check if any device parameters were provided
    bool hasVideoDevice = false;
    bool hasAudioDevice = false;

    // Handle linuxav scheme: linuxav://video=/dev/video0,audio=@DEFAULT_SOURCE@
    // or linuxav:///dev/video0 (simple video device path)
    if (input.substr(0, 9) == "linuxav:/") {
      if (input.substr(0, 10) == "linuxav://") {
        // Parse structured format: linuxav://video=/dev/video0,audio=@DEFAULT_SOURCE@
        std::string params = input.substr(10);

        size_t videoPos = params.find("video=");
        size_t audioPos = params.find("audio=");

        if (videoPos != std::string::npos) {
          size_t videoEnd = params.find(",", videoPos);
          if (videoEnd == std::string::npos) videoEnd = params.length();
          selectedVideoDevice = params.substr(videoPos + 6, videoEnd - videoPos - 6);
          hasVideoDevice = true;
          hasVideo = true;
        }

        if (audioPos != std::string::npos) {
          size_t audioEnd = params.find(",", audioPos);
          if (audioEnd == std::string::npos) audioEnd = params.length();
          selectedAudioDevice = params.substr(audioPos + 6, audioEnd - audioPos - 6);
          hasAudioDevice = true;
          hasAudio = true;
        }
      } else {
        // Simple format: linuxav:///dev/video0 (just video device)
        selectedVideoDevice = input.substr(9);
        if (selectedVideoDevice.substr(0, 5) != "/dev/") {
          selectedVideoDevice = "/dev/" + selectedVideoDevice;
        }
        hasVideoDevice = true;
        hasVideo = true;
        hasAudio = false;
      }
    }

    // Check command-line device options (override URL params)
    if (config->hasOption("video-device") && !config->getString("video-device").empty()) {
      selectedVideoDevice = config->getString("video-device");
      hasVideoDevice = true;
      hasVideo = true;
    }

    if (config->hasOption("audio-device") && !config->getString("audio-device").empty()) {
      selectedAudioDevice = resolveAudioDevice(config->getString("audio-device"));
      hasAudioDevice = true;
      hasAudio = true;
    }

    // Auto-detect ONLY if no device parameters were provided at all
    if (!hasVideoDevice && !hasAudioDevice) {
      INFO_MSG("No device parameters provided, auto-detecting first available devices");

      // Auto-detect first video device
      std::vector<std::string> videoDevices = getVideoDevices();
      if (!videoDevices.empty()) {
        selectedVideoDevice = videoDevices[0];
        hasVideo = true;
        INFO_MSG("Auto-detected video device: %s", selectedVideoDevice.c_str());
      } else {
        WARN_MSG("No video devices found, video capture disabled");
        hasVideo = false;
      }

      // Auto-detect first audio device
      std::vector<std::string> audioDevices = getAudioDevices();
      if (!audioDevices.empty()) {
        selectedAudioDevice = audioDevices[0];
        hasAudio = true;
        INFO_MSG("Auto-detected audio device: %s", selectedAudioDevice.c_str());
      } else {
        WARN_MSG("No audio devices found or PulseAudio server unavailable, audio capture disabled");
        hasAudio = false;
      }
    } else {
      // Resolve partial names for any provided audio device
      if (hasAudioDevice) { selectedAudioDevice = resolveAudioDevice(selectedAudioDevice); }
    }

    // Read audio configuration parameters
    if (hasAudio) {
      sampleRate = config->getInteger("samplerate");
      if (sampleRate <= 0) sampleRate = 48000;

      channels = config->getInteger("channels");
      if (channels <= 0) channels = 2;

      bufferSize = config->getInteger("buffersize");
      if (bufferSize <= 0) bufferSize = 1024;

      inputGain = atof(config->getString("gain").c_str());
      if (inputGain <= 0.0) inputGain = 0.5;
    }

    // Final validation
    if (!hasVideo && !hasAudio) {
      FAIL_MSG("No video or audio devices available");
      return false;
    }

    INFO_MSG("LinuxAV configuration: video=%s (enabled=%s), audio=%s (enabled=%s)",
             selectedVideoDevice.c_str(), hasVideo ? "yes" : "no", selectedAudioDevice.c_str(),
             hasAudio ? "yes" : "no");

    return true;
#endif
  }

  bool LinuxAV::preRun() {
#ifdef __linux__
    if (hasAudio && !initializePulseAudio()) {
      ERROR_MSG("Failed to initialize PulseAudio");
      return false;
    }
    return true;
#else
    return false;
#endif
  }

  bool LinuxAV::readHeader() {
#ifdef __linux__
    meta.reInit(streamName, false);

    if (hasVideo && !selectedVideoDevice.empty()) {
      if (!areDevicesAvailable()) {
        ERROR_MSG("Video device not available");
        return false;
      }
    }

    if (hasAudio && !selectedAudioDevice.empty()) {
      if (!areDevicesAvailable()) {
        ERROR_MSG("Audio device not available");
        return false;
      }
    }

    INFO_MSG("A/V Input header read completed: video=%s, audio=%s",
             hasVideo ? "enabled" : "disabled", hasAudio ? "enabled" : "disabled");
    return true;
#else
    return false;
#endif
  }

  void LinuxAV::streamMainLoop() {
#ifdef __linux__
    INFO_MSG("Starting A/V capture loop");

    // Audio mainloop thread is already started in initializePulseAudio()
    // Just wait for stream to be ready if audio is enabled
    if (hasAudio) {
      if (!waitForStreamReady()) {
        ERROR_MSG("Failed to create PulseAudio stream");
        return;
      }
    }

    INFO_MSG("A/V capture ready, starting main loop");
    isCapturing = true;
    startTimestamp = Util::bootMS();

    // Main processing loop
    while (config->is_active && isCapturing) {
      // Process video if enabled
      if (hasVideo && videoFd >= 0) {
        // Dequeue the filled buffer from the drivers outgoing queue
        HIGH_MSG("About to DQBUF video buffer (type=%d, index=%d)", videoBufferInfo.type,
                 videoBufferInfo.index);
        if (ioctl(videoFd, VIDIOC_DQBUF, &videoBufferInfo) < 0) {
          ERROR_MSG("Could not dequeue video buffer: %s", strerror(errno));
          break;
        }
        HIGH_MSG("DQBUF successful");

        // Get bytes used from the appropriate location
        size_t bytesUsed;
        if (videoBufferType == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
          bytesUsed = videoBufferInfo.m.planes[0].bytesused;
        } else {
          bytesUsed = videoBufferInfo.bytesused;
        }

        HIGH_MSG("Video buffer has %zu bytes", bytesUsed);
        if (!bytesUsed) {
          WARN_MSG("No video data available");
        } else {
          thisTime = Util::bootMS() - startTimestamp;
          thisIdx = videoTrackIdx;
          bufferLivePacket(thisTime, 0, videoTrackIdx, videoBuffer, bytesUsed, 0, true);
          frameCount++;
          MEDIUM_MSG("Buffered video packet, frame count: %lu", frameCount);
        }

        // Queue the buffer again for the next frame
        HIGH_MSG("About to QBUF video buffer for next frame (type=%d, index=%d)", videoBufferInfo.type,
                 videoBufferInfo.index);
        if (ioctl(videoFd, VIDIOC_QBUF, &videoBufferInfo) < 0) {
          ERROR_MSG("Could not enqueue video buffer: %s", strerror(errno));
          break;
        }
        HIGH_MSG("QBUF successful");
      }

      // Process audio if enabled
      if (hasAudio) {
        std::unique_lock<std::mutex> lock(bufferMutex);
        bufferCondition.wait_for(lock, std::chrono::milliseconds(100),
                                 [this] { return !audioQueue.empty() || !isCapturing; });

        if (!isCapturing) break;

        while (!audioQueue.empty() && config->is_active) {
          std::vector<uint8_t> audioData = audioQueue.front();
          uint64_t timestamp = timestampQueue.front();
          audioQueue.pop();
          timestampQueue.pop();
          lock.unlock();

          // Buffer the audio packet
          thisTime = timestamp - startTimestamp;
          thisIdx = audioTrackIdx;
          bufferLivePacket(thisTime, 0, audioTrackIdx,
                           reinterpret_cast<const char *>(audioData.data()), audioData.size(), 0, true);

          lock.lock();
        }
      }

      // Small delay to prevent excessive CPU usage
      if (!hasVideo) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    }

    INFO_MSG("A/V capture loop finished, captured %lu frames", frameCount);
#endif
  }

  bool LinuxAV::openStreamSource() {
#ifdef __linux__
    bool success = true;

    if (hasVideo) { success &= setupVideoDevice(); }
    if (hasAudio) { success &= setupAudioDevice(); }

    if (!success) {
      ERROR_MSG("Failed to setup devices");
      return false;
    }

    meta.setLive(true);
    meta.setVod(false);

    if (hasVideo && !selectedVideoDevice.empty()) {
      std::string pixFmtStr = intToString(videoPixelFmt);

      // Create video track with proper handling for raw vs compressed formats
      size_t staticSize = Util::pixfmtToSize(pixFmtStr, videoWidth, videoHeight);
      if (staticSize) {
        // Known static frame sizes: raw track mode
        videoTrackIdx = meta.addTrack(0, 0, 0, 0, true, staticSize);
      } else {
        // Other cases: standard track mode
        videoTrackIdx = meta.addTrack();
      }

      meta.setType(videoTrackIdx, "video");
      meta.setID(videoTrackIdx, 1);
      meta.setWidth(videoTrackIdx, videoWidth);
      meta.setHeight(videoTrackIdx, videoHeight);
      meta.setFpks(videoTrackIdx, 1000 * videoFpsDenominator / videoFpsNumerator);

      if (pixFmtStr == "MJPG") {
        meta.setCodec(videoTrackIdx, "JPEG");
      } else if (pixFmtStr == "YUYV") {
        meta.setCodec(videoTrackIdx, "YUYV");
      } else if (pixFmtStr == "UYVY") {
        meta.setCodec(videoTrackIdx, "UYVY");
      } else if (pixFmtStr == "BGR3") {
        meta.setCodec(videoTrackIdx, "BGR3");
      } else if (pixFmtStr == "NV24") {
        meta.setCodec(videoTrackIdx, "NV24");
      } else if (pixFmtStr == "NV16") {
        meta.setCodec(videoTrackIdx, "NV16");
      } else if (pixFmtStr == "NV12") {
        meta.setCodec(videoTrackIdx, "NV12");
      } else {
        FAIL_MSG("Unsupported pixel format %s, aborting", pixFmtStr.c_str());
        return false;
      }
    }

    if (hasAudio && !selectedAudioDevice.empty()) {
      audioTrackIdx = meta.addTrack();
      meta.setType(audioTrackIdx, "audio");
      meta.setCodec(audioTrackIdx, "FLOAT");
      meta.setID(audioTrackIdx, 2);
      meta.setRate(audioTrackIdx, sampleRate);
      meta.setChannels(audioTrackIdx, channels);
      meta.setSize(audioTrackIdx, 32);
    }

    if (hasVideo && videoTrackIdx != INVALID_TRACK_ID) {
      if (!userSelect.count(videoTrackIdx)) {
        userSelect[videoTrackIdx].reload(streamName, videoTrackIdx,
                                         COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }
    }
    if (hasAudio && audioTrackIdx != INVALID_TRACK_ID) {
      if (!userSelect.count(audioTrackIdx)) {
        userSelect[audioTrackIdx].reload(streamName, audioTrackIdx,
                                         COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }
    }

    INFO_MSG("A/V Input tracks created: video=%s, audio=%s", hasVideo ? "enabled" : "disabled",
             hasAudio ? "enabled" : "disabled");

    return true;
#else
    return false;
#endif
  }

  void LinuxAV::closeStreamSource() {
#ifdef __linux__
    isCapturing = false;

    // Cleanup video
    if (hasVideo) { cleanupVideoDevice(); }

    // Cleanup audio
    if (hasAudio) { cleanupAudioDevice(); }

    INFO_MSG("A/V Input resources cleaned up");
#endif
  }

  JSON::Value LinuxAV::enumerateSources(const std::string & device) {
#ifdef __linux__
    JSON::Value result;

    // Enumerate video devices (following V4L2 pattern)
    std::vector<std::string> videoDevices = getVideoDevices();
    for (const auto & dev : videoDevices) { result.append("linuxav:" + dev); }

    // Enumerate audio devices
    std::vector<std::string> audioDevices = getAudioDevices();
    for (const auto & dev : audioDevices) { result.append("linuxav:audio=" + dev); }

    return result;
#else
    return JSON::Value();
#endif
  }

  JSON::Value LinuxAV::getSourceCapa(const std::string & device) {
#ifdef __linux__
    JSON::Value result = capa;

    // Parse device to determine what capabilities to query
    std::string videoDevice = "";
    std::string audioDevice = "";

    if (device.find("video=") != std::string::npos) {
      size_t videoPos = device.find("video=");
      size_t videoEnd = device.find(",", videoPos);
      if (videoEnd == std::string::npos) videoEnd = device.length();
      videoDevice = device.substr(videoPos + 6, videoEnd - videoPos - 6);
    }

    if (device.find("audio=") != std::string::npos) {
      size_t audioPos = device.find("audio=");
      size_t audioEnd = device.find(",", audioPos);
      if (audioEnd == std::string::npos) audioEnd = device.length();
      audioDevice = device.substr(audioPos + 6, audioEnd - audioPos - 6);
    }

    // Get video capabilities if video device specified (following V4L2 pattern)
    if (!videoDevice.empty()) {
      if (videoDevice.substr(0, 5) != "/dev/") { videoDevice = "/dev/" + videoDevice; }

      int testFd = open(videoDevice.c_str(), O_RDWR);
      if (testFd >= 0) {
        result["optional"]["format"]["short"] = "F";
        result["optional"]["format"]["type"] = "string";
        JSON::Value & opts = result["optional"]["format"]["datalist"];

        uint64_t maxWidth = 0, maxHeight = 0;
        std::string defaultFormat = "";

        // Query the device for pixel formats (following V4L2 comprehensive approach)
        struct v4l2_fmtdesc fmt;
        fmt.index = 0;
        for (v4l2_buf_type capType : {V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE}) {
          fmt.type = capType;
          while (ioctl(testFd, VIDIOC_ENUM_FMT, &fmt) >= 0) {
            // For each pixel format, query supported resolutions
            struct v4l2_frmsizeenum frmSizes;
            frmSizes.pixel_format = fmt.pixelformat;
            frmSizes.index = 0;
            while (ioctl(testFd, VIDIOC_ENUM_FRAMESIZES, &frmSizes) >= 0) {
              if (frmSizes.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                // For each frame size, query supported FPS values
                struct v4l2_frmivalenum frmIntervals;
                memset(&frmIntervals, 0, sizeof(frmIntervals));
                frmIntervals.pixel_format = fmt.pixelformat;
                frmIntervals.width = frmSizes.discrete.width;
                frmIntervals.height = frmSizes.discrete.height;
                bool setHighestFPS = false;
                if (frmSizes.discrete.width * frmSizes.discrete.height > maxWidth * maxHeight) {
                  maxWidth = frmSizes.discrete.width;
                  maxHeight = frmSizes.discrete.height;
                  setHighestFPS = true;
                }
                ioctl(testFd, VIDIOC_ENUM_FRAMEINTERVALS, &frmIntervals);
                double maxFPS = 0;
                while (ioctl(testFd, VIDIOC_ENUM_FRAMEINTERVALS, &frmIntervals) != -1) {
                  if (frmIntervals.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
                    double fps =
                      (double)frmIntervals.discrete.denominator / (double)frmIntervals.discrete.numerator;
                    std::stringstream ss;
                    ss << intToString(fmt.pixelformat) << "-" << frmSizes.discrete.width << "x"
                       << frmSizes.discrete.height << "@";
                    ss.setf(std::ios::fixed);
                    ss.precision(2);
                    ss << fps;
                    opts.append(ss.str());
                    if (setHighestFPS && fps >= maxFPS) {
                      maxFPS = fps;
                      defaultFormat = ss.str();
                    }
                  }
                  frmIntervals.index += 1;
                }
              } else if (frmSizes.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
                // For continuous frame sizes, use the actual range reported by the device
                std::stringstream ss;
                ss << intToString(fmt.pixelformat) << "-" << frmSizes.stepwise.max_width << "x"
                   << frmSizes.stepwise.max_height << "@30.00";
                opts.append(ss.str());

                if (frmSizes.stepwise.max_width * frmSizes.stepwise.max_height > maxWidth * maxHeight) {
                  maxWidth = frmSizes.stepwise.max_width;
                  maxHeight = frmSizes.stepwise.max_height;
                  defaultFormat = ss.str();
                }

                // Also add the minimum resolution for reference
                if (frmSizes.stepwise.min_width != frmSizes.stepwise.max_width ||
                    frmSizes.stepwise.min_height != frmSizes.stepwise.max_height) {
                  std::stringstream ss_min;
                  ss_min << intToString(fmt.pixelformat) << "-" << frmSizes.stepwise.min_width
                         << "x" << frmSizes.stepwise.min_height << "@30.00";
                  opts.append(ss_min.str());
                }
              } else if (frmSizes.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
                // For stepwise frame sizes, use the maximum resolution
                std::stringstream ss;
                ss << intToString(fmt.pixelformat) << "-" << frmSizes.stepwise.max_width << "x"
                   << frmSizes.stepwise.max_height << "@30.00";
                opts.append(ss.str());

                if (frmSizes.stepwise.max_width * frmSizes.stepwise.max_height > maxWidth * maxHeight) {
                  maxWidth = frmSizes.stepwise.max_width;
                  maxHeight = frmSizes.stepwise.max_height;
                  defaultFormat = ss.str();
                }

                // Also add the minimum resolution for reference
                if (frmSizes.stepwise.min_width != frmSizes.stepwise.max_width ||
                    frmSizes.stepwise.min_height != frmSizes.stepwise.max_height) {
                  std::stringstream ss_min;
                  ss_min << intToString(fmt.pixelformat) << "-" << frmSizes.stepwise.min_width
                         << "x" << frmSizes.stepwise.min_height << "@30.00";
                  opts.append(ss_min.str());
                }
              }
              frmSizes.index++;
            }
            fmt.index++;
          }
        }

        if (!defaultFormat.empty()) { result["optional"]["format"]["default"] = defaultFormat; }

        close(testFd);
      }
    }

    // Get audio capabilities if audio device specified
    if (!audioDevice.empty()) {
      result["audio_formats"].append("PCM");
      result["audio_sample_rates"].append(8000);
      result["audio_sample_rates"].append(16000);
      result["audio_sample_rates"].append(22050);
      result["audio_sample_rates"].append(44100);
      result["audio_sample_rates"].append(48000);
      result["audio_sample_rates"].append(96000);
      result["audio_channels"].append(1);
      result["audio_channels"].append(2);
      result["audio_channels"].append(4);
      result["audio_channels"].append(6);
      result["audio_channels"].append(8);
    }

    return result;
#else
    return JSON::Value();
#endif
  }

#ifdef __linux__
  // === Video Methods (from V4L2) ===

  bool LinuxAV::setupVideoDevice() {
    // Open video device
    INFO_MSG("Opening video device %s", selectedVideoDevice.c_str());
    videoFd = open(selectedVideoDevice.c_str(), O_RDWR);
    if (videoFd < 0) {
      ERROR_MSG("Failed to open video device %s", selectedVideoDevice.c_str());
      return false;
    }

    // Configure video format
    if (!configureVideoFormat()) {
      ERROR_MSG("Failed to configure video format");
      close(videoFd);
      videoFd = -1;
      return false;
    }

    // Set video format and resolution
    struct v4l2_format imageFormat;
    imageFormat.type = videoBufferType;
    if (videoBufferType == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
      imageFormat.fmt.pix_mp.width = videoWidth;
      imageFormat.fmt.pix_mp.height = videoHeight;
      imageFormat.fmt.pix_mp.pixelformat = videoPixelFmt;
      imageFormat.fmt.pix_mp.field = V4L2_FIELD_NONE;
    } else {
      imageFormat.fmt.pix.width = videoWidth;
      imageFormat.fmt.pix.height = videoHeight;
      imageFormat.fmt.pix.pixelformat = videoPixelFmt;
      imageFormat.fmt.pix.field = V4L2_FIELD_NONE;
    }
    if (ioctl(videoFd, VIDIOC_S_FMT, &imageFormat) < 0) {
      ERROR_MSG("Could not apply video format");
      close(videoFd);
      videoFd = -1;
      return false;
    }

    // Set framerate
    struct v4l2_streamparm streamParam;
    streamParam.type = videoBufferType;
    if (ioctl(videoFd, VIDIOC_G_PARM, &streamParam) != 0) {
      WARN_MSG("Device does not support stream parameters (common for HDMI capture) - skipping "
               "framerate setting");
    } else {
      streamParam.parm.capture.capturemode |= V4L2_CAP_TIMEPERFRAME;
      streamParam.parm.capture.timeperframe.denominator = videoFpsDenominator;
      streamParam.parm.capture.timeperframe.numerator = videoFpsNumerator;
      if (ioctl(videoFd, VIDIOC_S_PARM, &streamParam) != 0) {
        WARN_MSG("Could not set stream parameters - device may not support framerate control");
      } else {
        INFO_MSG("Framerate set to %.1f fps", (float)videoFpsDenominator / (float)videoFpsNumerator);
      }
    }

    // Setup memory mapping
    v4l2_requestbuffers requestBuffer = {0};
    requestBuffer.count = 1;
    requestBuffer.type = videoBufferType;
    requestBuffer.memory = V4L2_MEMORY_MMAP;
    if (ioctl(videoFd, VIDIOC_REQBUFS, &requestBuffer) < 0) {
      ERROR_MSG("Could not request video buffers");
      close(videoFd);
      videoFd = -1;
      return false;
    }

    // Map buffer
    v4l2_buffer queryBuffer = {0};
    queryBuffer.type = videoBufferType;
    queryBuffer.memory = V4L2_MEMORY_MMAP;
    queryBuffer.index = 0;

    // For multi-planar, we need to allocate the planes array
    v4l2_plane planes[VIDEO_MAX_PLANES];
    if (videoBufferType == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
      memset(planes, 0, sizeof(planes));
      queryBuffer.m.planes = planes;

      // Get the actual number of planes from the format
      struct v4l2_format fmt;
      memset(&fmt, 0, sizeof(fmt));
      fmt.type = videoBufferType;
      if (ioctl(videoFd, VIDIOC_G_FMT, &fmt) < 0) {
        ERROR_MSG("Could not get format to determine number of planes");
        close(videoFd);
        videoFd = -1;
        return false;
      }

      numPlanes = fmt.fmt.pix_mp.num_planes;
      queryBuffer.length = numPlanes;
      INFO_MSG("Multi-planar format detected with %d planes", numPlanes);
    }

    if (ioctl(videoFd, VIDIOC_QUERYBUF, &queryBuffer) < 0) {
      ERROR_MSG("Unable to query video buffer");
      close(videoFd);
      videoFd = -1;
      return false;
    }

    size_t bufferLength;
    __u32 bufferOffset;
    if (videoBufferType == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
      bufferLength = queryBuffer.m.planes[0].length;
      bufferOffset = queryBuffer.m.planes[0].m.mem_offset;
    } else {
      bufferLength = queryBuffer.length;
      bufferOffset = queryBuffer.m.offset;
    }

    videoBuffer = (char *)mmap(NULL, bufferLength, PROT_READ | PROT_WRITE, MAP_SHARED, videoFd, bufferOffset);
    memset(videoBuffer, 0, bufferLength);

    // Initialize buffer info
    memset(&videoBufferInfo, 0, sizeof(videoBufferInfo));
    videoBufferInfo.type = videoBufferType;
    videoBufferInfo.memory = V4L2_MEMORY_MMAP;
    videoBufferInfo.index = 0;

    // For multi-planar, we need to setup the planes array for streaming operations too
    if (videoBufferType == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
      memset(videoPlanes, 0, sizeof(videoPlanes));
      videoBufferInfo.m.planes = videoPlanes;

      // Get the actual number of planes from the format
      struct v4l2_format fmt;
      memset(&fmt, 0, sizeof(fmt));
      fmt.type = videoBufferType;
      if (ioctl(videoFd, VIDIOC_G_FMT, &fmt) < 0) {
        ERROR_MSG("Could not get format to determine number of planes for streaming");
        close(videoFd);
        videoFd = -1;
        return false;
      }

      numPlanes = fmt.fmt.pix_mp.num_planes;
      videoBufferInfo.length = numPlanes;
      INFO_MSG("Multi-planar streaming setup with %d planes", numPlanes);

      // Copy the plane information from the queryBuffer to our streaming planes
      for (uint32_t i = 0; i < numPlanes; i++) {
        videoPlanes[i].length = queryBuffer.m.planes[i].length;
        videoPlanes[i].bytesused = 0; // Will be filled by driver during DQBUF
        videoPlanes[i].m.mem_offset = queryBuffer.m.planes[i].m.mem_offset;
        videoPlanes[i].data_offset = 0;
      }
      HIGH_MSG("Initialized %d planes for streaming with proper buffer info", numPlanes);
    }

    // Start streaming
    int type = videoBufferInfo.type;
    HIGH_MSG("About to start streaming with type=%d", type);
    if (ioctl(videoFd, VIDIOC_STREAMON, &type) < 0) {
      ERROR_MSG("Unable to start video streaming: %s", strerror(errno));
      close(videoFd);
      videoFd = -1;
      return false;
    }
    HIGH_MSG("Video streaming started successfully");

    // Queue the initial buffer for capture
    HIGH_MSG("Queueing initial buffer for capture (type=%d, index=%d)", videoBufferInfo.type, videoBufferInfo.index);
    if (ioctl(videoFd, VIDIOC_QBUF, &videoBufferInfo) < 0) {
      ERROR_MSG("Could not queue initial video buffer: %s", strerror(errno));
      close(videoFd);
      videoFd = -1;
      return false;
    }
    HIGH_MSG("Initial buffer queued successfully");

    INFO_MSG("Video device setup completed: %lux%lu @ %.1f fps", videoWidth, videoHeight,
             (float)videoFpsDenominator / (float)videoFpsNumerator);
    return true;
  }

  void LinuxAV::cleanupVideoDevice() {
    if (videoFd >= 0) {
      int type = videoBufferInfo.type;
      if (ioctl(videoFd, VIDIOC_STREAMOFF, &type) < 0) {
        ERROR_MSG("Could not stop video streaming");
      }
      close(videoFd);
      videoFd = -1;
    }
    if (videoBuffer) {
      // Unmap the video buffer memory
      // Note: We need to store the buffer length to properly unmap
      // For now, we'll just set to nullptr since the process cleanup will handle it
      videoBuffer = nullptr;
    }
  }

  std::vector<std::string> LinuxAV::getVideoDevices() {
    std::vector<std::string> devices;

    DIR *d = opendir("/sys/class/video4linux");
    if (!d) {
      WARN_MSG("Unable to enumerate video devices. Is v4l2 available on the system?");
      return devices;
    }

    // Cycle through all devices
    struct dirent *dp;
    do {
      errno = 0;
      if ((dp = readdir(d))) {
        // Only consider devices starting with video
        if (dp->d_type != DT_LNK || strncmp(dp->d_name, "video", 5) != 0) { continue; }

        // Open FD to the corresponding /dev/videoN device
        std::string path = "/dev/" + std::string(dp->d_name);
        int testFd = open(path.c_str(), O_RDWR);
        if (testFd < 0) {
          WARN_MSG("Failed to check device %s, continuing", dp->d_name);
          continue;
        }

        // Query the device for any video input capabilities
        struct v4l2_fmtdesc fmt;
        fmt.index = 0;
        bool hasCapture = false;

        // Check both capture types like getSourceCapa does
        for (v4l2_buf_type capType : {V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE}) {
          fmt.type = capType;
          fmt.index = 0;
          if (ioctl(testFd, VIDIOC_ENUM_FMT, &fmt) >= 0) {
            hasCapture = true;
            break;
          }
        }

        if (hasCapture) {
          devices.push_back(path);
          INFO_MSG("Found video device: %s", path.c_str());
        }
        close(testFd);
      }
    } while (dp != NULL);

    closedir(d);
    return devices;
  }

  std::vector<std::string> LinuxAV::getAudioDevices() {
    std::vector<std::string> devices;

    INFO_MSG("Starting PulseAudio device enumeration...");

    // Create temporary PulseAudio context for enumeration
    pa_mainloop *tempMainloop = pa_mainloop_new();
    if (!tempMainloop) {
      WARN_MSG("Failed to create PulseAudio mainloop - audio disabled");
      return devices;
    }

    pa_mainloop_api *tempApi = pa_mainloop_get_api(tempMainloop);
    pa_context *tempContext = pa_context_new(tempApi, "MistServer Source Enumeration");
    if (!tempContext) {
      pa_mainloop_free(tempMainloop);
      WARN_MSG("Failed to create PulseAudio context - audio disabled");
      return devices;
    }

    // Connect to PulseAudio server
    INFO_MSG("Connecting to PulseAudio server...");
    if (pa_context_connect(tempContext, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
      WARN_MSG("Failed to connect to PulseAudio server - audio disabled");
      pa_context_unref(tempContext);
      pa_mainloop_free(tempMainloop);
      return devices;
    }

    // Wait for connection
    bool connected = false;
    int timeout = 0;
    while (!connected && timeout < 100) {
      if (pa_mainloop_iterate(tempMainloop, 0, nullptr) < 0) { break; }
      pa_context_state_t state = pa_context_get_state(tempContext);
      if (state == PA_CONTEXT_READY) {
        connected = true;
        INFO_MSG("PulseAudio context connected successfully");
      } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        ERROR_MSG("PulseAudio context failed or terminated");
        break;
      }
      timeout++;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!connected) {
      WARN_MSG("PulseAudio server connection timeout - audio disabled");
      pa_context_disconnect(tempContext);
      pa_context_unref(tempContext);
      pa_mainloop_free(tempMainloop);
      return devices;
    }

    // Get source list
    struct SourceEnumData {
        std::vector<std::string> *devices;
        pa_mainloop *mainloop;
        bool done;
    } enumData = {&devices, tempMainloop, false};

    INFO_MSG("Requesting PulseAudio source list...");

    auto sourceListCallback = +[](pa_context *c, const pa_source_info *info, int eol, void *userdata) {
      SourceEnumData *data = static_cast<SourceEnumData *>(userdata);
      if (eol) {
        INFO_MSG("End of PulseAudio source list reached");
        data->done = true;
        return;
      }
      if (info && info->name) {
        data->devices->push_back(info->name);
        INFO_MSG("Found PulseAudio source: %s (%s)", info->name,
                 info->description ? info->description : "no description");
      }
    };

    pa_operation *op = pa_context_get_source_info_list(tempContext, sourceListCallback, &enumData);
    if (op) {
      INFO_MSG("PulseAudio operation started, waiting for completion...");
      while (!enumData.done) {
        if (pa_mainloop_iterate(tempMainloop, 1, nullptr) < 0) {
          ERROR_MSG("PulseAudio mainloop iteration failed");
          break;
        }
      }
      pa_operation_unref(op);
      INFO_MSG("PulseAudio operation completed");
    } else {
      ERROR_MSG("Failed to start PulseAudio source enumeration operation");
    }

    // Cleanup
    pa_context_disconnect(tempContext);
    pa_context_unref(tempContext);
    pa_mainloop_free(tempMainloop);

    if (devices.empty()) {
      WARN_MSG("No PulseAudio sources found - audio disabled");
    } else {
      INFO_MSG("Found %zu PulseAudio source(s)", devices.size());
    }

    return devices;
  }

  std::string LinuxAV::resolveAudioDevice(const std::string & partialName) {
    if (partialName.empty()) { return "@DEFAULT_SOURCE@"; }

    // If it's already a full device name or special name, use as-is
    if (partialName.find("@") == 0 || partialName.find(".") != std::string::npos) {
      return partialName;
    }

    // Check if it's a numeric index
    bool isNumeric = true;
    for (char c : partialName) {
      if (!std::isdigit(c)) {
        isNumeric = false;
        break;
      }
    }

    std::vector<std::string> devices = getAudioDevices();

    if (isNumeric) {
      size_t index = std::stoul(partialName);
      if (index < devices.size()) {
        INFO_MSG("Resolved audio device index %zu to: %s", index, devices[index].c_str());
        return devices[index];
      } else {
        WARN_MSG("Audio device index %zu out of range (0-%zu)", index, devices.size() - 1);
        return partialName;
      }
    }

    // Try partial name matching
    std::vector<std::string> matches;
    for (const auto & device : devices) {
      if (device.find(partialName) == 0) { // Starts with partial name
        matches.push_back(device);
      }
    }

    if (matches.empty()) {
      // Try case-insensitive matching
      std::string lowerPartial = partialName;
      std::transform(lowerPartial.begin(), lowerPartial.end(), lowerPartial.begin(), ::tolower);

      for (const auto & device : devices) {
        std::string lowerDevice = device;
        std::transform(lowerDevice.begin(), lowerDevice.end(), lowerDevice.begin(), ::tolower);
        if (lowerDevice.find(lowerPartial) == 0) { matches.push_back(device); }
      }
    }

    if (matches.size() == 1) {
      INFO_MSG("Resolved partial audio device '%s' to: %s", partialName.c_str(), matches[0].c_str());
      return matches[0];
    } else if (matches.size() > 1) {
      WARN_MSG("Ambiguous audio device '%s' matches %zu devices:", partialName.c_str(), matches.size());
      for (size_t i = 0; i < matches.size(); ++i) {
        WARN_MSG("  [%zu]: %s", i, matches[i].c_str());
      }
      INFO_MSG("Using first match: %s", matches[0].c_str());
      return matches[0];
    } else {
      WARN_MSG("No audio device found matching '%s'", partialName.c_str());
      return partialName;
    }
  }

  bool LinuxAV::configureVideoFormat() {
    // Parse format from config if provided
    std::string format = "";
    if (config->hasOption("format") && !config->getString("format").empty()) {
      format = config->getString("format");

      // Parse format: MJPG-1920x1080@30.00
      size_t fmtDelPos = format.find('-');
      if (fmtDelPos != std::string::npos) {
        std::string pixFmtStr = format.substr(0, fmtDelPos);
        videoPixelFmt = strToInt(pixFmtStr);
        format = format.substr(fmtDelPos + 1);

        // Parse resolution
        size_t resolutionDelPos = format.find('@');
        size_t widthDelPos = format.find('x');
        if (resolutionDelPos != std::string::npos && widthDelPos != std::string::npos) {
          videoWidth = atoi(format.substr(0, widthDelPos).c_str());
          format = format.substr(widthDelPos + 1);
          videoHeight = atoi(format.substr(0, resolutionDelPos - widthDelPos - 1).c_str());
          format = format.substr(resolutionDelPos - widthDelPos);
        } else if (widthDelPos != std::string::npos) {
          videoWidth = atoi(format.substr(0, widthDelPos).c_str());
          videoHeight = atoi(format.substr(widthDelPos + 1).c_str());
        } else {
          ERROR_MSG("Unable to find resolution in requested format %s", config->getString("format").c_str());
          return false;
        }
        // Remaining string is the target FPS, which we will match to a fraction in the following loop
      } else {
        ERROR_MSG("Unable to find pixel format in requested format %s", config->getString("format").c_str());
        return false;
      }
    }

    // Try to detect active input first (for HDMI inputs)
    bool foundActiveInput = false;
    if (!videoPixelFmt || !videoWidth || !videoHeight) {
      INFO_MSG("Attempting to detect active input format...");

      // Check both capture types
      for (v4l2_buf_type capType : {V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE}) {
        struct v4l2_format activeFormat;
        memset(&activeFormat, 0, sizeof(activeFormat));
        activeFormat.type = capType;

        if (ioctl(videoFd, VIDIOC_G_FMT, &activeFormat) >= 0) {
          if (activeFormat.fmt.pix.width > 0 && activeFormat.fmt.pix.height > 0 &&
              activeFormat.fmt.pix.pixelformat > 0) {
            videoBufferType = capType;
            videoWidth = activeFormat.fmt.pix.width;
            videoHeight = activeFormat.fmt.pix.height;
            videoPixelFmt = activeFormat.fmt.pix.pixelformat;
            foundActiveInput = true;

            std::string detectedFormat = intToString(videoPixelFmt);
            INFO_MSG("Detected active input: %s %lux%lu", detectedFormat.c_str(), videoWidth, videoHeight);
            break;
          }
        }
      }
    }

    // If we found an active input, use it and skip enumeration
    if (foundActiveInput) {
      // Set default FPS if not specified
      if (!videoFpsDenominator || !videoFpsNumerator) {
        videoFpsDenominator = 30;
        videoFpsNumerator = 1;
        INFO_MSG("Using default FPS for active input: 30.0");
      }

      std::string pixFmtStr = intToString(videoPixelFmt);
      INFO_MSG("Using active input format: %s %lux%lu @ %.1f fps", pixFmtStr.c_str(), videoWidth,
               videoHeight, (float)videoFpsDenominator / (float)videoFpsNumerator);
      return true;
    }

    // Set defaults for unset parameters, set FPS and sanity checks
    struct v4l2_fmtdesc fmt;
    fmt.index = 0;
    bool hasFPS = format.size(); // Automatically adjust FPS if none was set
    bool hasResolution = videoWidth && videoHeight; // Automatically adjust resolution if none was set
    bool hasPixFmt = videoPixelFmt; // Automatically adjust pixel format if none was set

    // Check both capture types like enumeration and getSourceCapa do
    for (v4l2_buf_type capType : {V4L2_BUF_TYPE_VIDEO_CAPTURE, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE}) {
      fmt.type = capType;
      fmt.index = 0;

      INFO_MSG("Checking buffer type %s", (capType == V4L2_BUF_TYPE_VIDEO_CAPTURE) ? "CAPTURE" : "CAPTURE_MPLANE");

      while (ioctl(videoFd, VIDIOC_ENUM_FMT, &fmt) >= 0) {
        std::string detectedFormat = intToString(fmt.pixelformat);
        INFO_MSG("Found format: %s (%u)", detectedFormat.c_str(), fmt.pixelformat);

        // If we have a requested pixelFmt, skip any non-matching formats
        if (hasPixFmt && fmt.pixelformat != videoPixelFmt) {
          fmt.index++;
          continue;
        }

        // Go through supported resolution and FPS combos
        struct v4l2_frmsizeenum frmSizes;
        frmSizes.pixel_format = fmt.pixelformat;
        frmSizes.index = 0;
        bool foundValidSize = false;

        while (ioctl(videoFd, VIDIOC_ENUM_FRAMESIZES, &frmSizes) >= 0) {
          if (frmSizes.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            INFO_MSG("  Discrete resolution: %ux%u", frmSizes.discrete.width, frmSizes.discrete.height);
            foundValidSize = true;

            if (!hasResolution) {
              // If we have no resolution set, select the largest supported surface area
              if (frmSizes.discrete.width * frmSizes.discrete.height > videoWidth * videoHeight) {
                INFO_MSG("  Selecting format %s with resolution %ux%u", detectedFormat.c_str(),
                         frmSizes.discrete.width, frmSizes.discrete.height);
                videoWidth = frmSizes.discrete.width;
                videoHeight = frmSizes.discrete.height;
                videoPixelFmt = fmt.pixelformat;
              } else {
                // Current surface area is lower, so skip it
                frmSizes.index++;
                continue;
              }
            } else if (frmSizes.discrete.width != videoWidth || frmSizes.discrete.height != videoHeight) {
              // Current resolution does not match requested resolution, so skip it
              frmSizes.index++;
              continue;
            }

            // Check supported FPS values for this resolution
            struct v4l2_frmivalenum frmIntervals;
            memset(&frmIntervals, 0, sizeof(frmIntervals));
            frmIntervals.pixel_format = videoPixelFmt;
            frmIntervals.width = videoWidth;
            frmIntervals.height = videoHeight;
            ioctl(videoFd, VIDIOC_ENUM_FRAMEINTERVALS, &frmIntervals);
            while (ioctl(videoFd, VIDIOC_ENUM_FRAMEINTERVALS, &frmIntervals) != -1) {
              if (frmIntervals.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
                if (!hasFPS) {
                  // If we have no FPS set, select the largest FPS we can get for the current resolution
                  if (videoFpsNumerator &&
                      (float)frmIntervals.discrete.denominator / (float)frmIntervals.discrete.numerator <=
                        (float)videoFpsDenominator / (float)videoFpsNumerator) {
                    // Current FPS is lower, so skip it
                    frmIntervals.index++;
                    continue;
                  }
                } else if (int(frmIntervals.discrete.denominator / frmIntervals.discrete.numerator) !=
                           atoi(format.c_str())) {
                  // Current FPS does not match requested FPS, so skip it
                  frmIntervals.index++;
                  continue;
                }
                // Store the denominator and numerator for the requested FPS
                videoFpsDenominator = frmIntervals.discrete.denominator;
                videoFpsNumerator = frmIntervals.discrete.numerator;
              }
              frmIntervals.index++;
            }
          } else if (frmSizes.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
            INFO_MSG("  Continuous frame size: %ux%u to %ux%u", frmSizes.stepwise.min_width,
                     frmSizes.stepwise.min_height, frmSizes.stepwise.max_width, frmSizes.stepwise.max_height);
            foundValidSize = true;

            if (!hasResolution) {
              // For continuous, use the maximum resolution available
              videoWidth = frmSizes.stepwise.max_width;
              videoHeight = frmSizes.stepwise.max_height;
              videoPixelFmt = fmt.pixelformat;
              INFO_MSG("  Using max continuous resolution: %lux%lu", videoWidth, videoHeight);
            } else {
              // Check if requested resolution is within range
              if (videoWidth >= frmSizes.stepwise.min_width && videoWidth <= frmSizes.stepwise.max_width &&
                  videoHeight >= frmSizes.stepwise.min_height && videoHeight <= frmSizes.stepwise.max_height) {
                videoPixelFmt = fmt.pixelformat;
                INFO_MSG("  Requested resolution %lux%lu is within continuous range", videoWidth, videoHeight);
              }
            }
          } else if (frmSizes.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
            INFO_MSG("  Stepwise frame size: %ux%u to %ux%u (step %ux%u)", frmSizes.stepwise.min_width,
                     frmSizes.stepwise.min_height, frmSizes.stepwise.max_width, frmSizes.stepwise.max_height,
                     frmSizes.stepwise.step_width, frmSizes.stepwise.step_height);
            foundValidSize = true;

            if (!hasResolution) {
              // For stepwise, use the maximum resolution available
              videoWidth = frmSizes.stepwise.max_width;
              videoHeight = frmSizes.stepwise.max_height;
              videoPixelFmt = fmt.pixelformat;
              INFO_MSG("  Using max stepwise resolution: %lux%lu", videoWidth, videoHeight);
            } else {
              // Check if requested resolution fits stepwise pattern
              if (videoWidth >= frmSizes.stepwise.min_width && videoWidth <= frmSizes.stepwise.max_width &&
                  videoHeight >= frmSizes.stepwise.min_height && videoHeight <= frmSizes.stepwise.max_height) {
                // Check if it aligns with step size
                uint32_t widthSteps = (videoWidth - frmSizes.stepwise.min_width) / frmSizes.stepwise.step_width;
                uint32_t heightSteps =
                  (videoHeight - frmSizes.stepwise.min_height) / frmSizes.stepwise.step_height;
                if ((frmSizes.stepwise.min_width + widthSteps * frmSizes.stepwise.step_width) == videoWidth &&
                    (frmSizes.stepwise.min_height + heightSteps * frmSizes.stepwise.step_height) == videoHeight) {
                  videoPixelFmt = fmt.pixelformat;
                  INFO_MSG("  Requested resolution %lux%lu fits stepwise pattern", videoWidth, videoHeight);
                }
              }
            }
          }
          frmSizes.index++;
        }

        // If we found valid sizes but no discrete FPS info, set default FPS
        if (foundValidSize && !videoFpsDenominator && !videoFpsNumerator) {
          videoFpsDenominator = 30;
          videoFpsNumerator = 1;
          INFO_MSG("  Using default FPS: 30.0");
        }

        fmt.index++;
      }

      // If we found a valid format, break out of the buffer type loop
      if (videoPixelFmt != 0) {
        videoBufferType = capType;
        break;
      }
    }

    // If we still have no format/resolution, but detected active input, use that
    if (!videoPixelFmt && foundActiveInput) {
      ERROR_MSG(
        "No supported formats found, but active input was detected - this shouldn't happen");
      return false;
    }

    // Abort if this input does not support the requested pixel format
    std::string pixFmtStr = intToString(videoPixelFmt);
    if (pixFmtStr != "MJPG" && pixFmtStr != "YUYV" && pixFmtStr != "UYVY" && pixFmtStr != "BGR3" &&
        pixFmtStr != "NV24" && pixFmtStr != "NV16" && pixFmtStr != "NV12") {
      ERROR_MSG("Unsupported pixel format %s (%d), aborting", pixFmtStr.c_str(), videoPixelFmt);
      return false;
    }

    // Abort if we have no resolution
    if (!videoWidth || !videoHeight) {
      ERROR_MSG("Unable to determine resolution, aborting");
      return false;
    }

    // Set default FPS if still not set
    if (!videoFpsDenominator || !videoFpsNumerator) {
      videoFpsDenominator = 30;
      videoFpsNumerator = 1;
      INFO_MSG("Using default FPS: 30.0");
    }

    INFO_MSG("Video format configured: %s %lux%lu @ %.1f fps", pixFmtStr.c_str(), videoWidth,
             videoHeight, (float)videoFpsDenominator / (float)videoFpsNumerator);
    return true;
  }

  bool LinuxAV::configureAudioFormat() {
    // Configure sample spec
    sampleSpec.format = PA_SAMPLE_FLOAT32LE;
    sampleSpec.rate = sampleRate;
    sampleSpec.channels = channels;

    if (!pa_sample_spec_valid(&sampleSpec)) {
      ERROR_MSG("Invalid PulseAudio sample specification");
      return false;
    }

    // Configure channel map
    pa_channel_map_init_stereo(&channelMap);
    if (channels != 2) {
      pa_channel_map_init_extend(&channelMap, channels, PA_CHANNEL_MAP_DEFAULT);
    }

    // Configure buffer attributes
    bufferAttr.maxlength = (uint32_t)-1;
    bufferAttr.fragsize = bufferSize * channels * sizeof(float);
    bufferAttr.minreq = (uint32_t)-1;
    bufferAttr.prebuf = (uint32_t)-1;
    bufferAttr.tlength = (uint32_t)-1;

    INFO_MSG("Audio format configured: %uHz, %u channels, float32", sampleRate, channels);
    return true;
  }

  // === Audio Methods (from PulseAudio) ===

  void LinuxAV::bufferAudioData(const void *buffer, size_t length, uint64_t timestamp) {
    if (!buffer || length == 0) return;

    // Apply input gain to prevent clipping
    std::vector<uint8_t> processedBuffer(length);
    const float *input = static_cast<const float *>(buffer);
    float *output = reinterpret_cast<float *>(processedBuffer.data());
    size_t sampleCount = length / sizeof(float);

    for (size_t i = 0; i < sampleCount; ++i) {
      float sample = input[i] * inputGain;
      // Soft clipping to prevent distortion
      if (sample > 1.0f)
        sample = 1.0f;
      else if (sample < -1.0f)
        sample = -1.0f;
      output[i] = sample;
    }

    // Add to buffer queue
    {
      std::lock_guard<std::mutex> lock(bufferMutex);
      audioQueue.push(std::move(processedBuffer));
      timestampQueue.push(timestamp);

      // Limit buffer size to prevent memory issues
      while (audioQueue.size() > 100) {
        audioQueue.pop();
        timestampQueue.pop();
      }
    }
    bufferCondition.notify_one();
  }

  bool LinuxAV::setupAudioDevice() {
    if (!contextReady) {
      ERROR_MSG("PulseAudio not connected");
      return false;
    }

    // Configure audio format
    if (!configureAudioFormat()) {
      ERROR_MSG("Failed to configure audio format");
      return false;
    }

    // Create PulseAudio stream
    if (!createPulseAudioStream()) {
      ERROR_MSG("Failed to create PulseAudio stream");
      return false;
    }

    INFO_MSG("Audio device setup completed for %s", selectedAudioDevice.c_str());
    return true;
  }

  void LinuxAV::cleanupAudioDevice() {
    // Stop and cleanup stream
    if (stream) {
      pa_stream_disconnect(stream);
      pa_stream_unref(stream);
      stream = nullptr;
    }

    // Cleanup context
    if (context) {
      pa_context_disconnect(context);
      pa_context_unref(context);
      context = nullptr;
    }

    // Stop main loop
    if (mainloop) { pa_mainloop_quit(mainloop, 0); }

    // Wait for main loop thread
    if (mainloopThread.joinable()) { mainloopThread.join(); }

    // Cleanup main loop
    if (mainloop) {
      pa_mainloop_free(mainloop);
      mainloop = nullptr;
    }

    mainloopApi = nullptr;
    contextReady = false;
    streamReady = false;
  }

  // === Device Management ===

  bool LinuxAV::setVideoDevice(const std::string & devicePath) {
    selectedVideoDevice = devicePath;
    INFO_MSG("Video device set to: %s", selectedVideoDevice.c_str());
    return true;
  }

  bool LinuxAV::setAudioDevice(const std::string & deviceName) {
    if (deviceName.empty()) {
      selectedAudioDevice = "@DEFAULT_SOURCE@";
    } else {
      selectedAudioDevice = deviceName;
    }
    INFO_MSG("Audio device set to: %s", selectedAudioDevice.c_str());
    return true;
  }

  bool LinuxAV::areDevicesAvailable() {
    bool videoAvailable = true;
    bool audioAvailable = true;

    // Check video device availability
    if (hasVideo && !selectedVideoDevice.empty()) {
      int testFd = open(selectedVideoDevice.c_str(), O_RDWR);
      if (testFd < 0) {
        videoAvailable = false;
      } else {
        // Verify it's actually a video capture device
        struct v4l2_capability cap;
        if (ioctl(testFd, VIDIOC_QUERYCAP, &cap) < 0 || !(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
          videoAvailable = false;
        }
        close(testFd);
      }
    }

    // Check audio device availability
    if (hasAudio && !selectedAudioDevice.empty()) {
      // For PulseAudio, we'll do a quick connection test
      pa_mainloop *testMainloop = pa_mainloop_new();
      if (testMainloop) {
        pa_mainloop_api *testApi = pa_mainloop_get_api(testMainloop);
        pa_context *testContext = pa_context_new(testApi, "MistServer Device Test");
        if (testContext) {
          if (pa_context_connect(testContext, nullptr, PA_CONTEXT_NOFLAGS, nullptr) >= 0) {
            // Wait briefly for connection
            int iterations = 0;
            while (iterations < 50) {
              if (pa_mainloop_iterate(testMainloop, 0, nullptr) < 0) break;
              pa_context_state_t state = pa_context_get_state(testContext);
              if (state == PA_CONTEXT_READY) {
                break;
              } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
                audioAvailable = false;
                break;
              }
              iterations++;
            }
          } else {
            audioAvailable = false;
          }
          pa_context_disconnect(testContext);
          pa_context_unref(testContext);
        } else {
          audioAvailable = false;
        }
        pa_mainloop_free(testMainloop);
      } else {
        audioAvailable = false;
      }
    }

    return (!hasVideo || videoAvailable) && (!hasAudio || audioAvailable);
  }

  // === PulseAudio Callbacks ===

  void LinuxAV::streamReadCallback(pa_stream *stream, size_t length, void *userdata) {
    LinuxAV *input = static_cast<LinuxAV *>(userdata);
    if (!input || !stream) return;

    const void *data;
    size_t bytesRead;

    if (pa_stream_peek(stream, &data, &bytesRead) < 0) {
      ERROR_MSG("Failed to read from PulseAudio stream: %s", pa_strerror(pa_context_errno(input->context)));
      return;
    }

    if (data && bytesRead > 0) {
      uint64_t timestamp = Util::bootMS();
      input->bufferAudioData(data, bytesRead, timestamp);
    }

    pa_stream_drop(stream);
  }

  void LinuxAV::streamStateCallback(pa_stream *stream, void *userdata) {
    LinuxAV *input = static_cast<LinuxAV *>(userdata);
    if (!input || !stream) return;

    pa_stream_state_t state = pa_stream_get_state(stream);

    switch (state) {
      case PA_STREAM_READY:
        INFO_MSG("PulseAudio stream ready");
        input->streamReady = true;
        break;
      case PA_STREAM_FAILED:
        ERROR_MSG("PulseAudio stream failed: %s", pa_strerror(pa_context_errno(input->context)));
        input->streamReady = false;
        break;
      case PA_STREAM_TERMINATED:
        INFO_MSG("PulseAudio stream terminated");
        input->streamReady = false;
        break;
      default: break;
    }
  }

  void LinuxAV::contextStateCallback(pa_context *c, void *userdata) {
    LinuxAV *input = static_cast<LinuxAV *>(userdata);
    pa_context_state_t state = pa_context_get_state(c);

    switch (state) {
      case PA_CONTEXT_CONNECTING: INFO_MSG("PulseAudio context connecting..."); break;
      case PA_CONTEXT_AUTHORIZING: INFO_MSG("PulseAudio context authorizing..."); break;
      case PA_CONTEXT_SETTING_NAME: INFO_MSG("PulseAudio context setting name..."); break;
      case PA_CONTEXT_READY:
        INFO_MSG("PulseAudio context ready");
        input->contextReady = true;
        break;
      case PA_CONTEXT_FAILED:
        ERROR_MSG("PulseAudio context failed: %s", pa_strerror(pa_context_errno(c)));
        input->contextReady = false;
        break;
      case PA_CONTEXT_TERMINATED:
        INFO_MSG("PulseAudio context terminated");
        input->contextReady = false;
        break;
      default: break;
    }
  }

  void LinuxAV::sourceInfoCallback(pa_context *context, const pa_source_info *info, int eol, void *userdata) {
    if (eol) return;

    LinuxAV *input = static_cast<LinuxAV *>(userdata);
    if (!input || !info) return;

    INFO_MSG("Found PulseAudio source: %s (%s)", info->name,
             info->description ? info->description : info->name);
  }

  void LinuxAV::mainloopThreadFunction() {
    INFO_MSG("PulseAudio main loop thread started");

    while (config->is_active) { pa_mainloop_iterate(mainloop, 1, NULL); }

    INFO_MSG("PulseAudio main loop thread finished");
  }

  bool LinuxAV::waitForStreamReady(uint32_t timeout_ms) {
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(timeout_ms);

    while (!streamReady && config->is_active) {
      if (std::chrono::steady_clock::now() - start > timeout) {
        ERROR_MSG("Timeout waiting for PulseAudio stream");
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return streamReady;
  }

  bool LinuxAV::initializePulseAudio() {
    INFO_MSG("Starting PulseAudio initialization...");

    // Create main loop
    mainloop = pa_mainloop_new();
    if (!mainloop) {
      ERROR_MSG("Failed to create PulseAudio main loop");
      return false;
    }

    mainloopApi = pa_mainloop_get_api(mainloop);
    if (!mainloopApi) {
      ERROR_MSG("Failed to get PulseAudio main loop API");
      return false;
    }

    // Create context
    context = pa_context_new(mainloopApi, "MistServer A/V Input");
    if (!context) {
      ERROR_MSG("Failed to create PulseAudio context");
      return false;
    }

    // Set context state callback
    pa_context_set_state_callback(context, contextStateCallback, this);

    // Connect to server
    if (pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
      ERROR_MSG("Failed to connect to PulseAudio server");
      return false;
    }

    // Wait for context to be ready using mainloop iteration
    contextReady = false;
    while (!contextReady) {
      pa_mainloop_iterate(mainloop, 1, NULL);

      pa_context_state_t state = pa_context_get_state(context);
      if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        ERROR_MSG("PulseAudio context failed to become ready");
        return false;
      }
    }

    INFO_MSG("PulseAudio context ready");

    // Now start the mainloop thread for ongoing operations
    mainloopThread = std::thread(&LinuxAV::mainloopThreadFunction, this);

    return true;
  }

  bool LinuxAV::createPulseAudioStream() {
    if (!context || !contextReady) {
      ERROR_MSG("PulseAudio context not ready");
      return false;
    }

    // Create stream
    stream = pa_stream_new(context, "MistServer A/V Audio Input", &sampleSpec, &channelMap);
    if (!stream) {
      ERROR_MSG("Failed to create PulseAudio stream: %s", pa_strerror(pa_context_errno(context)));
      return false;
    }

    // Set stream callbacks
    pa_stream_set_state_callback(stream, streamStateCallback, this);
    pa_stream_set_read_callback(stream, streamReadCallback, this);

    // Connect stream
    pa_stream_flags_t flags =
      static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY | PA_STREAM_AUTO_TIMING_UPDATE);

    const char *deviceName = selectedAudioDevice.c_str();
    if (selectedAudioDevice == "@DEFAULT_SOURCE@") {
      deviceName = nullptr; // Use default
    }

    if (pa_stream_connect_record(stream, deviceName, &bufferAttr, flags) < 0) {
      ERROR_MSG("Failed to connect PulseAudio stream: %s", pa_strerror(pa_context_errno(context)));
      return false;
    }

    INFO_MSG("PulseAudio stream created and connected");
    return true;
  }

  std::string LinuxAV::intToString(int n) {
    std::string output;
    while (n) {
      output += (char)n & 0xFF;
      n >>= 8;
    }
    return output;
  }

  int LinuxAV::strToInt(const std::string & str) {
    int output = 0;
    for (int i = str.size() - 1; i >= 0; i--) {
      output <<= 8;
      output += (char)str[i];
    }
    return output;
  }

#endif // __linux__

} // namespace Mist

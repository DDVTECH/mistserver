// Combined Audio/Video input for Linux local capture
// This plugin provides combined audio/video input capabilities using V4L2 for video and PulseAudio for audio. It's
// designed for local capture scenarios where you want both audio and video from local devices - video capture cards,
// USB cameras, built-in cameras, microphones, line-in, or any other V4L2/PulseAudio compatible devices.
//
// Source format examples:
//- linuxav://                                      (auto-detect first available devices)
//- linuxav://video=/dev/video0                     (video only, auto-detect audio)
//- linuxav://audio=@DEFAULT_SOURCE@                (audio only, auto-detect video)
//- linuxav://video=/dev/video0,audio=@DEFAULT_SOURCE@

#pragma once

#include "input.h"

#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/util.h>

#include <condition_variable>
#include <linux/videodev2.h>
#include <mutex>
#ifdef WITH_PULSE
#include <pulse/pulseaudio.h>
#endif
#include <queue>
#include <thread>

namespace Mist {
  class LinuxAV : public Input {
    public:
      LinuxAV(Util::Config *cfg);
      ~LinuxAV();

    protected:
      bool checkArguments();
      bool preRun();
      bool readHeader();
      bool needHeader() { return false; }
      bool isSingular() { return true; }
      bool needsLock() { return false; }

      bool openStreamSource();
      void streamMainLoop();
      void closeStreamSource();

      JSON::Value enumerateSources(const std::string & device);
      JSON::Value getSourceCapa(const std::string & device);

      // V4L2 related
      bool setupVideoDevice();
      std::vector<std::string> getVideoDevices();
      bool configureVideoFormat();
      void cleanupVideoDevice();
      // V4l2 variables
      int videoFd; ///< Video device file descriptor
      v4l2_buffer videoBufferInfo; ///< Video buffer info
      char *videoBuffer; ///< Video buffer pointer
      uint64_t videoWidth; ///< Video width
      uint64_t videoHeight; ///< Video height
      uint64_t videoFpsDenominator; ///< Video FPS denominator
      uint64_t videoFpsNumerator; ///< Video FPS numerator
      uint32_t videoPixelFmt; ///< Video pixel format
      v4l2_buf_type videoBufferType; ///< Video buffer type (capture vs mplane)
      std::string selectedVideoDevice; ///< Selected video device path
      v4l2_plane videoPlanesQ[80]; ///< Video planes query for multi-planar buffers
      v4l2_plane videoPlanesB[80]; ///< Video planes buffer for multi-planar buffers
      uint32_t numPlanes; ///< Number of planes for multi-planar buffers

#ifdef WITH_PULSE
      // Pulseaudio related
      std::vector<std::string> getAudioDevices();
      std::string resolveAudioDevice(const std::string & partialName);
      void bufferAudioData(const void *buffer, size_t length);
      // Audio parameters
      uint32_t sampleRate{48000}; ///< Sample rate in Hz
      uint32_t channels{2}; ///< Number of channels
      uint32_t bufferSize{0}; ///< Audio buffer size in samples
      float inputGain{0.5f}; ///< Input gain level (0.0-1.0)
      // Audio variables
      pa_mainloop *mainloop{0}; ///< PulseAudio main loop
      pa_mainloop_api *mainloopApi{0}; ///< Main loop API
      pa_context *context{0}; ///< PulseAudio context
      pa_stream *stream{0}; ///< PulseAudio stream
      std::string selectedAudioDevice; ///< Selected audio device name
      bool contextReady{false}; ///< Audio context ready flag
      bool streamReady{false}; ///< Audio stream ready flag
      std::thread mainloopThread; ///< PulseAudio mainloop thread
      Util::ResizeablePointer audioData; ///< Audio data buffer
      uint64_t audioTime{0}; ///< Millisecond timestamp of the first value in the audioData buffer
      std::mutex bufferMutex; ///< Mutex for buffer access
      size_t wholeMs{48}; ///< How many samples is a while number of millis?
#endif

      // State tracking
      bool isCapturing; ///< Capture state flag
      bool hasVideo{false}; ///< Video capture enabled
      bool hasAudio{false}; ///< Audio capture enabled
      uint64_t startTimestamp{0}; ///< Capture start timestamp
      size_t videoTrackIdx{INVALID_TRACK_ID}; ///< Video track index
      size_t audioTrackIdx{INVALID_TRACK_ID}; ///< Audio track index
      uint64_t frameCount{0}; ///< Total frames captured
  };
} // namespace Mist

typedef Mist::LinuxAV mistIn;

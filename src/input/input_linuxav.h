#pragma once

#include "input.h"

#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/util.h>

#ifdef __linux__
#include <linux/videodev2.h>
#include <pulse/pulseaudio.h>
#endif

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#define VIDEO_MAX_PLANES 8 // Maximum number of planes for multi-planar buffers

namespace Mist {
  /**
   * @brief Combined Audio/Video input for Linux local capture
   *
   * This plugin provides combined audio/video input capabilities using V4L2 for video
   * and PulseAudio for audio. It's designed for local capture scenarios where you want both
   * audio and video from local devices - video capture cards, USB cameras, built-in cameras,
   * microphones, line-in, or any other V4L2/PulseAudio compatible devices.
   *
   * Source format examples:
   * - linuxav://                                      (auto-detect first available devices)
   * - linuxav://video=/dev/video0                     (video only, auto-detect audio)
   * - linuxav://audio=@DEFAULT_SOURCE@                (audio only, auto-detect video)
   * - linuxav://video=/dev/video0,audio=@DEFAULT_SOURCE@
   *
   */
  class LinuxAV : public Input {
    public:
      LinuxAV(Util::Config *cfg);
      ~LinuxAV();

    protected:
      /**
       * @brief Validates configuration arguments
       * @return true if arguments are valid
       */
      bool checkArguments();

      /**
       * @brief Prepares for capture
       * @return true if preparation successful
       */
      bool preRun();

      /**
       * @brief Reads stream header information
       * @return true if header read successfully
       */
      bool readHeader();

      /**
       * @brief Indicates if header is needed
       * @return false - local input doesn't need header
       */
      bool needHeader() { return false; }

      /**
       * @brief Indicates if this is a singular input
       * @return true - local input is always singular
       */
      bool isSingular() { return true; }

      /**
       * @brief Indicates if locking is needed
       * @return false - local input doesn't need locking
       */
      bool needsLock() { return false; }

      /**
       * @brief Main streaming loop
       */
      void streamMainLoop();

      /**
       * @brief Opens the stream source
       * @return true if source opened successfully
       */
      bool openStreamSource();

      /**
       * @brief Closes the stream source
       */
      void closeStreamSource();

      /**
       * @brief Enumerates available audio/video sources
       * @param device Device identifier
       * @return JSON object with available sources
       */
      JSON::Value enumerateSources(const std::string & device);

      /**
       * @brief Gets source capabilities
       * @param device Device identifier
       * @return JSON object with device capabilities
       */
      JSON::Value getSourceCapa(const std::string & device);

#ifdef __linux__
      // === Video Capture Methods (from V4L2) ===

      /**
       * @brief Sets up V4L2 video device
       * @return true if setup successful
       */
      bool setupVideoDevice();

      /**
       * @brief Cleans up V4L2 resources
       */
      void cleanupVideoDevice();

      /**
       * @brief Gets list of available video devices
       * @return Vector of device paths
       */
      std::vector<std::string> getVideoDevices();

      /**
       * @brief Configures video format parameters
       * @return true if configuration successful
       */
      bool configureVideoFormat();

      // === Audio Capture Methods (from PulseAudio) ===

      /**
       * @brief Buffers audio data from PulseAudio
       * @param buffer Audio buffer data
       * @param length Buffer length in bytes
       * @param timestamp Timestamp in milliseconds
       */
      void bufferAudioData(const void *buffer, size_t length, uint64_t timestamp);

      /**
       * @brief Sets up PulseAudio context and stream
       * @return true if setup successful
       */
      bool setupAudioDevice();

      /**
       * @brief Cleans up PulseAudio resources
       */
      void cleanupAudioDevice();

      /**
       * @brief Gets list of available input devices
       * @return Vector of device names
       */
      std::vector<std::string> getAudioDevices();

      /**
       * @brief Resolves partial audio device name to full device name
       * @param partialName Partial device name, numeric index, or full name
       * @return Full device name or original input if no match
       */
      std::string resolveAudioDevice(const std::string & partialName);

      /**
       * @brief Configures audio format parameters
       * @return true if configuration successful
       */
      bool configureAudioFormat();

      // === Device Management ===

      /**
       * @brief Sets the video device
       * @param devicePath Device path (e.g., /dev/video0)
       * @return true if device set successfully
       */
      bool setVideoDevice(const std::string & devicePath);

      /**
       * @brief Sets the audio device
       * @param deviceName Device name or index
       * @return true if device set successfully
       */
      bool setAudioDevice(const std::string & deviceName);

      /**
       * @brief Checks if devices are available
       * @return true if both devices are available
       */
      bool areDevicesAvailable();

      // === PulseAudio Callback Methods ===
      static void contextStateCallback(pa_context *context, void *userdata);
      static void streamStateCallback(pa_stream *stream, void *userdata);
      static void streamReadCallback(pa_stream *stream, size_t length, void *userdata);
      static void sourceInfoCallback(pa_context *context, const pa_source_info *info, int eol, void *userdata);

      // === Video Member Variables ===
      int videoFd; ///< Video device file descriptor
      v4l2_buffer videoBufferInfo; ///< Video buffer info
      char *videoBuffer; ///< Video buffer pointer

      // Video parameters
      uint64_t videoWidth; ///< Video width
      uint64_t videoHeight; ///< Video height
      uint64_t videoFpsDenominator; ///< Video FPS denominator
      uint64_t videoFpsNumerator; ///< Video FPS numerator
      uint32_t videoPixelFmt; ///< Video pixel format
      v4l2_buf_type videoBufferType; ///< Video buffer type (capture vs mplane)
      std::string selectedVideoDevice; ///< Selected video device path

      // Multi-planar buffer support
      v4l2_plane videoPlanes[VIDEO_MAX_PLANES]; ///< Video planes for multi-planar buffers
      uint32_t numPlanes; ///< Number of planes for multi-planar buffers

      // === Audio Member Variables ===
      pa_mainloop *mainloop; ///< PulseAudio main loop
      pa_mainloop_api *mainloopApi; ///< Main loop API
      pa_context *context; ///< PulseAudio context
      pa_stream *stream; ///< PulseAudio stream
      pa_sample_spec sampleSpec; ///< Sample specification
      pa_channel_map channelMap; ///< Channel map
      pa_buffer_attr bufferAttr; ///< Buffer attributes

      std::string selectedAudioDevice; ///< Selected audio device name

      // Audio parameters
      uint32_t sampleRate; ///< Sample rate in Hz
      uint32_t channels; ///< Number of channels
      uint32_t bufferSize; ///< Audio buffer size in bytes
      float inputGain; ///< Input gain level (0.0-1.0)

      // Threading and buffering
      std::mutex bufferMutex; ///< Mutex for buffer access
      std::condition_variable bufferCondition; ///< Condition variable for buffer
      std::queue<std::vector<uint8_t>> audioQueue; ///< Audio data queue
      std::queue<uint64_t> timestampQueue; ///< Timestamp queue
      std::thread mainloopThread; ///< PulseAudio mainloop thread

      // State tracking
      bool isCapturing; ///< Capture state flag
      bool hasVideo; ///< Video capture enabled
      bool hasAudio; ///< Audio capture enabled
      bool contextReady; ///< Audio context ready flag
      bool streamReady; ///< Audio stream ready flag
      uint64_t startTimestamp; ///< Capture start timestamp
      size_t videoTrackIdx; ///< Video track index
      size_t audioTrackIdx; ///< Audio track index
      uint64_t frameCount; ///< Total frames captured

      // Error handling
      std::string lastErrorString; ///< Last error description

      /**
       * @brief Thread function for PulseAudio mainloop
       */
      void mainloopThreadFunction();

      /**
       * @brief Waits for audio stream to be ready
       * @param timeout_ms Timeout in milliseconds
       * @return true if stream became ready
       */
      bool waitForStreamReady(uint32_t timeout_ms = 5000);

      /**
       * @brief Initializes PulseAudio main loop and context
       * @return true if initialization successful
       */
      bool initializePulseAudio();

      /**
       * @brief Creates and connects PulseAudio stream
       * @return true if stream creation successful
       */
      bool createPulseAudioStream();

      /**
       * @brief Converts pixel format integer to string
       * @param n Pixel format integer
       * @return String representation of pixel format
       */
      std::string intToString(int n);

      /**
       * @brief Converts string to pixel format integer
       * @param str String representation of pixel format
       * @return Pixel format integer
       */
      int strToInt(const std::string & str);

#endif // __linux__
  };
} // namespace Mist

typedef Mist::LinuxAV mistIn;

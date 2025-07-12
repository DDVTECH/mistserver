#include <mist/defines.h>
#include <mist/stream.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <fcntl.h>
#include <sstream>
#include "input_v4l2.h"

namespace Mist{
  inputVideo4Linux::inputVideo4Linux(Util::Config *cfg) : Input(cfg){
    capa["name"] = "V4L2";
    capa["desc"] = "";
    capa["source_match"] = "v4l2:*";
    capa["always_match"] = capa["source_match"];
    capa["priority"] = 10;
    width = 0;
    height = 0;
    fpsDenominator = 0;
    fpsNumerator = 0;
    pixelFmt = 0;

    JSON::Value option;
    option["arg"] = "string";
    option["long"] = "format";
    option["short"] = "F";
    option["help"] = "Requested resolution, framerate and pixel format, like 'MJPG-1920x1080@90.00'. FPS is optional. Defaults to using the highest surface area and FPS if not given";
    option["value"].append("");
    config->addOption("format", option);

    capa["optional"]["format"]["name"] = "Device resolution, framerate and pixel format";
    capa["optional"]["format"]["help"] = "Requested format, like 'MJPG-1920x1080@90.00'. FPS is optional. Defaults to using the highest surface area and FPS if not given";
    capa["optional"]["format"]["option"] = "--format";
    capa["optional"]["format"]["short"] = "F";
    capa["optional"]["format"]["default"] = "";
    capa["optional"]["format"]["type"] = "string";

    capa["enum_static_prefix"] = "v4l2:";
    option.null();
    option["long"] = "enumerate";
    option["short"] = "e";
    option["help"] = "Output MistIn supported devices in JSON format, then exit";
    option["value"].append("");
    config->addOption("enumerate", option);

    capa["dynamic_capa"] = true;
    option.null();
    option["long"] = "getcapa";
    option["arg"] = "string";
    option["short"] = "q";
    option["help"] = "(string) Output device capabilities for given device in JSON format, then exit";
    option["value"].append("");
    config->addOption("getcapa", option);
  }

  /// @brief Writes a JSON list of connected video inputs to stdout
  JSON::Value inputVideo4Linux::enumerateSources(const std::string & device){
    JSON::Value output;
    DIR *d = opendir("/sys/class/video4linux");
    if (!d){
      FAIL_MSG("Unable to enumerate video devices. Is v4l2 available on the system?");
      return output;
    }

    // Cycle through all devices
    struct dirent *dp;
    do{
      errno = 0;
      if ((dp = readdir(d))){
        // Only consider devices starting with video
        if (dp->d_type != DT_LNK || strncmp(dp->d_name, "video", 5) != 0){continue;}

        // Open FD to the corresponding /dev/videoN device
        std::string path = "/dev/" + std::string(dp->d_name);
        fd = open(path.c_str() ,O_RDWR);
        if(fd < 0){
          FAIL_MSG("Failed to check device %s, continuing", dp->d_name);
          continue;
        }

        // Query the device for any video input capabilities
        struct v4l2_fmtdesc fmt;
        fmt.index = 0;
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) >= 0) {
          output.append("v4l2:"+path);
        }
        close(fd);
      }
    }while (dp != NULL);

    closedir(d);
    return output;
  }

  /// @brief Writes a JSON list compatible pixel formats, resolution and FPS for a video input to stdout
  /// \param device: path to the device to query
  JSON::Value inputVideo4Linux::getSourceCapa(const std::string & device){
    JSON::Value output = capa;
    std::string input = getInput(device);

    // Open FD to the corresponding device
    fd = open(input.c_str(), O_RDWR);
    if(fd < 0){
      FAIL_MSG("Failed to open device, aborting");
      return output;
    }

    output["optional"]["format"]["short"] = "F";
    output["optional"]["format"]["type"] = "string";
    JSON::Value & opts = output["optional"]["format"]["datalist"];


    // Query the device for pixel formats
    struct v4l2_fmtdesc fmt;
    fmt.index = 0;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) >= 0) {
      // For each pixel format, query supported resolutions
      struct v4l2_frmsizeenum frmSizes;
      frmSizes.pixel_format = fmt.pixelformat;
      frmSizes.index = 0;
      while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmSizes) >= 0) {
        // Only support discrete frame size types for now
        if (frmSizes.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
          // For each frame size, query supported FPS values
          struct v4l2_frmivalenum frmIntervals;
          memset(&frmIntervals, 0, sizeof(frmIntervals));
          frmIntervals.pixel_format = fmt.pixelformat;
          frmIntervals.width = frmSizes.discrete.width;
          frmIntervals.height = frmSizes.discrete.height;
          bool setHighestFPS = false;
          if (frmSizes.discrete.width * frmSizes.discrete.height > width * height){
            width = frmSizes.discrete.width;
            height = frmSizes.discrete.height;
            setHighestFPS = true;
          }
          ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmIntervals);
          double maxFPS = 0;
          while (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmIntervals) != -1) {
            if (frmIntervals.type == V4L2_FRMIVAL_TYPE_DISCRETE){
              double fps = (double)frmIntervals.discrete.denominator / (double)frmIntervals.discrete.numerator;
              std::stringstream ss;
              ss << intToString(fmt.pixelformat) << "-" << frmSizes.discrete.width << "x" << frmSizes.discrete.height << "@";
              ss.setf(std::ios::fixed);
              ss.precision(2);
              // Use a human readable format for FPS
              ss << fps;
              opts.append(ss.str());
              if (setHighestFPS && fps >= maxFPS){
                maxFPS = fps;
                output["optional"]["format"]["default"] = ss.str();
              }
            }
            frmIntervals.index += 1;
          }
        }
        frmSizes.index++;
      }
      fmt.index++;
    }

    close(fd);
    return output;
  }

  /// \brief Checks whether the device supports the given config and sets defaults for any missing properties
  bool inputVideo4Linux::checkArguments(){
    std::string input = getInput(config->getString("input"));

    // Open file descriptor to the requested device
    INFO_MSG("Opening video device %s", input.c_str());
    fd = open(input.c_str() ,O_RDWR);
    if(fd < 0){
      FAIL_MSG("Failed to open device %s, aborting", config->getString("input").c_str());
      return false;
    }

    // Init params to requested format if it was given
    // If not set, we will default to the highest surface area and pick
    //  the highest FPS the camera supports for that resolution
    std::string format = "";
    if (config->hasOption("format") && config->getString("format").size()){
      format = config->getString("format");

      // Anything before a - is the requested pixel format
      size_t fmtDelPos = format.find('-');
      if (fmtDelPos != std::string::npos){
        pixelFmt = strToInt(format.substr(0, fmtDelPos));
        format = format.substr(fmtDelPos + 1);
      }else{
        FAIL_MSG("Unable to find pixel format in requested format %s", config->getString("format").c_str());
        close(fd);
        return false;
      }

      // Anything before the @ sign is the resolution
      size_t resolutionDelPos = format.find('@');
      size_t widthDelPos = format.find('x');
      if (resolutionDelPos != std::string::npos && widthDelPos != std::string::npos){
        width = atoi(format.substr(0, widthDelPos).c_str());
        format = format.substr(widthDelPos + 1);
        height = atoi(format.substr(0, resolutionDelPos - widthDelPos - 1).c_str());
        format = format.substr(resolutionDelPos - widthDelPos);
      }else{
        FAIL_MSG("Unable to find resolution in requested format %s", config->getString("format").c_str());
        close(fd);
        return false;
      }
      // Remaining string is the target FPS, which we will match to a fraction in the following loop
    }

    // Set defaults for unset parameters, set FPS and sanity checks
    struct v4l2_fmtdesc fmt;
    fmt.index = 0;
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bool hasFPS = format.size(); //< Automatically adjust FPS is none was set
    bool hasResolution = width && height; //< Automatically adjust resolution is none was set
    bool hasPixFmt = pixelFmt; //< Automatically adjust pixel format is none was set
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) >= 0) {
      // If we have a requested pixelFmt, skip any non-matching formats
      if (hasPixFmt && fmt.pixelformat != pixelFmt){
        fmt.index++;
        continue;
      }

      // Else go through supported resolution and FPS combos
      struct v4l2_frmsizeenum frmSizes;
      frmSizes.pixel_format = fmt.pixelformat;
      frmSizes.index = 0;
      while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmSizes) >= 0) {
        if (frmSizes.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
          if (!hasResolution){
            // If we have no resolution set, select the largest supported surface area
            if (frmSizes.discrete.width * frmSizes.discrete.height > width * height){
              width = frmSizes.discrete.width;
              height = frmSizes.discrete.height;
              pixelFmt = fmt.pixelformat;
            }else{
              // Current surface area is lower, so skip it
              frmSizes.index++;
              continue;
            }
          }else if (frmSizes.discrete.width != width || frmSizes.discrete.height != height){
            // Current resolution does not match requested resolution, so skip it
            frmSizes.index++;
            continue;
          }

          // At this point we found the requested resolution or adjusted it upwards, so check supported FPS values
          struct v4l2_frmivalenum frmIntervals;
          memset(&frmIntervals, 0, sizeof(frmIntervals));
          frmIntervals.pixel_format = pixelFmt;
          frmIntervals.width = width;
          frmIntervals.height = height;
          ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmIntervals);
          while (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmIntervals) != -1) {
            if (frmIntervals.type == V4L2_FRMIVAL_TYPE_DISCRETE){
              if (!hasFPS){
                // If we have no FPS set, select the largest FPS we can get for the current resolution
                if (fpsNumerator && (float)frmIntervals.discrete.denominator / (float)frmIntervals.discrete.numerator
                  <= (float)fpsDenominator / (float)fpsNumerator){
                  // Current FPS is lower, so skip it
                  frmIntervals.index++;
                  continue;
                }
              }else if (int(frmIntervals.discrete.denominator / frmIntervals.discrete.numerator) != atoi(format.c_str())){
                // Current FPS does not match requested FPS, so skip it
                frmIntervals.index++;
                continue;
              }
              // Store the denominator and numerator for the requested FPS
              fpsDenominator = frmIntervals.discrete.denominator;
              fpsNumerator = frmIntervals.discrete.numerator;
            }
            frmIntervals.index++;
          }
        }
        frmSizes.index++;
      }
      fmt.index++;
    }

    // Abort if this input does not support the requested pixel format
    std::string pixFmtStr = intToString(pixelFmt);
    if (pixFmtStr != "MJPG" && pixFmtStr != "YUYV" && pixFmtStr != "UYVY") {
      FAIL_MSG("Unsupported pixel format %s, aborting", pixFmtStr.c_str());
      close(fd);
      return false;
    }

    // Abort if we have no resolution
    if (!width || !height) {
      FAIL_MSG("Unable to determine resolution, aborting");
      close(fd);
      return false;
    }

    // Abort if we have no FPS
    if (!fpsDenominator || !fpsNumerator) {
      FAIL_MSG("Unable to determine FPS, aborting");
      close(fd);
      return false;
    }

    return true;
  }

  /// \brief Applies config to the video device and maps its buffer to a local pointer
  bool inputVideo4Linux::openStreamSource(){
    if(fd < 0){
      FAIL_MSG("Lost connection to the device, aborting");
      return false;
    }
    std::string pixFmtStr = intToString(pixelFmt);
    INFO_MSG("Opening video device with pixel format %s, resolution %lux%lu @ %.1f fps", pixFmtStr.c_str(), width, height, (float)fpsDenominator / (float)fpsNumerator);

    // Set requested pixel format and resolution
    struct v4l2_format imageFormat;
    imageFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    imageFormat.fmt.pix.width = width;
    imageFormat.fmt.pix.height = height;
    imageFormat.fmt.pix.pixelformat = pixelFmt;
    imageFormat.fmt.pix.field = V4L2_FIELD_NONE;
    if(ioctl(fd, VIDIOC_S_FMT, &imageFormat) < 0){
      FAIL_MSG("Could not apply image parameters, aborting");
      close(fd);
      return false;
    }

    // Set requested framerate
    struct v4l2_streamparm streamParam;
    streamParam.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_PARM, &streamParam) != 0){
      FAIL_MSG("Could not apply stream parameters, aborting");
      close(fd);
      return false;
    }
    streamParam.parm.capture.capturemode |= V4L2_CAP_TIMEPERFRAME;
    streamParam.parm.capture.timeperframe.denominator = fpsDenominator;
    streamParam.parm.capture.timeperframe.numerator = fpsNumerator;
    if(ioctl(fd, VIDIOC_S_PARM, &streamParam) != 0){
      FAIL_MSG("Could not apply stream parameters, aborting");
      close(fd);
      return false;
    }

    // Initiate memory mapping
    v4l2_requestbuffers requestBuffer = {0};
    requestBuffer.count = 1;
    requestBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    requestBuffer.memory = V4L2_MEMORY_MMAP;
    if(ioctl(fd, VIDIOC_REQBUFS, &requestBuffer) < 0){
      FAIL_MSG("Could not initiate memory mapping, aborting");
      close(fd);
      return false;
    }

    // Query location of the buffers in device memory
    v4l2_buffer queryBuffer = {0};
    queryBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    queryBuffer.memory = V4L2_MEMORY_MMAP;
    queryBuffer.index = 0;
    if(ioctl(fd, VIDIOC_QUERYBUF, &queryBuffer) < 0){
      FAIL_MSG("Unable to query buffer information, aborting");
      close(fd);
      return false;
    }

    // Map buffer to local address space
    buffer = (char*)mmap(NULL, queryBuffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, queryBuffer.m.offset);
    memset(buffer, 0, queryBuffer.length);

    // Init buffer info struct, which is going to contain pointers to buffers and meta information
    memset(&bufferinfo, 0, sizeof(bufferinfo));
    bufferinfo.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bufferinfo.memory = V4L2_MEMORY_MMAP;
    bufferinfo.index = 0;

    // Activate streaming I/O
    int type = bufferinfo.type;
    if(ioctl(fd, VIDIOC_STREAMON, &type) < 0){
      FAIL_MSG("Unable to start streaming I/O, aborting");
      close(fd);
      return false;
    }

    // Create video track
    // Note: pixFmtStr is not always identical to "codec", but it is for all currently-supported raw formats at least
    size_t staticSize = Util::pixfmtToSize(pixFmtStr, imageFormat.fmt.pix.width, imageFormat.fmt.pix.height);
    if (staticSize){
      //Known static frame sizes: raw track mode
      tNumber = meta.addTrack(0, 0, 0, 0, true, staticSize);
    }else{
      // Other cases: standard track mode
      tNumber = meta.addTrack();
    }
    meta.setLive(true);
    meta.setVod(false);
    meta.setID(tNumber, tNumber);
    meta.setType(tNumber, "video");
    meta.setWidth(tNumber, imageFormat.fmt.pix.width);
    meta.setHeight(tNumber, imageFormat.fmt.pix.height);
    meta.setFpks(tNumber, 1000 * fpsDenominator / fpsNumerator);
    if (pixFmtStr == "MJPG"){
      meta.setCodec(tNumber, "JPEG");
    }else if (pixFmtStr == "YUYV"){
      meta.setCodec(tNumber, "YUYV");
    }else if (pixFmtStr == "UYVY"){
      meta.setCodec(tNumber, "UYVY");
    }else{
      FAIL_MSG("Unsupported pixel format %s, aborting", pixFmtStr.c_str());
      closeStreamSource();
      return false;
    }

    return true;
  }

  void inputVideo4Linux::streamMainLoop(){
    uint64_t statTimer = 0;
    uint64_t startTime = Util::bootSecs();
    uint64_t timeOffset = 0;
    if (tNumber){
      timeOffset = meta.getBootMsOffset();
    }else{
      timeOffset = Util::bootMS();
      meta.setBootMsOffset(timeOffset);
    }
    Comms::Connections statComm;
    thisIdx = tNumber;
    if (!userSelect.count(thisIdx)) { userSelect[thisIdx].reload(streamName, thisIdx, COMM_STATUS_ACTSOURCEDNT); }
    while (config->is_active && userSelect[thisIdx]){
      if (userSelect[thisIdx].getStatus() & COMM_STATUS_REQDISCONNECT){
        Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
        break;
      }

      // Enqueue an empty buffer to the driver's incoming queue
      if(ioctl(fd, VIDIOC_QBUF, &bufferinfo) < 0){
        ERROR_MSG("Could not enqueue buffer, aborting");
        return;
      }
      // Dequeue the filled buffer from the drivers outgoing queue
      if(ioctl(fd, VIDIOC_DQBUF, &bufferinfo) < 0){
        ERROR_MSG("Could not dequeue the buffer, aborting");
        return;
      }
      if (!bufferinfo.bytesused){
        Util::logExitReason(ER_CLEAN_EOF, "no more data");
        break;
      }
      INSANE_MSG("Buffer has %f KBytes of data", (double)bufferinfo.bytesused / 1024);
      thisIdx = tNumber;
      thisTime = Util::bootMS() - timeOffset;
      bufferLivePacket(thisTime, 0, tNumber, buffer, bufferinfo.bytesused, 0, true);

      if (!userSelect.count(thisIdx)) { userSelect[thisIdx].reload(streamName, thisIdx, COMM_STATUS_ACTSOURCEDNT); }
      if (Util::bootSecs() - statTimer > 1){
        // Connect to stats for INPUT detection
        if (!statComm){statComm.reload(streamName, getConnectedBinHost(), JSON::Value(getpid()).asString(), "INPUT:" + capa["name"].asStringRef(), "");}
        if (statComm){
          if (!statComm){
            config->is_active = false;
            Util::logExitReason(ER_CLEAN_CONTROLLER_REQ, "received shutdown request from controller");
            return;
          }
          uint64_t now = Util::bootSecs();
          statComm.setNow(now);
          statComm.setStream(streamName);
          statComm.setTime(now - startTime);
          statComm.setLastSecond(0);
          connStats(statComm);
        }

        statTimer = Util::bootSecs();
      }
    }
  }

  void inputVideo4Linux::closeStreamSource(){
    if (fd){
      int type = bufferinfo.type;
      if(ioctl(fd, VIDIOC_STREAMOFF, &type) < 0){
        ERROR_MSG("Could not stop camera streaming I/O");
      }
      close(fd);
    }
  }

  /// \brief converts an int representing an encoded string back to it's original form
  std::string inputVideo4Linux::intToString(int n){
    std::string output;
    while(n){
      output += (char)n & 0xFF;
      n >>= 8;
    }
    return output;
  }

  /// \brief Converts a string to a hex encoded int
  int inputVideo4Linux::strToInt(std::string str){
    int output = 0;
    for (int i = str.size() - 1; i >= 0; i--){
      output <<= 8;
      output += (char)str[i];
    }
    return output;
  }

  /// \brief Translates an input string to it's matching device path
  std::string inputVideo4Linux::getInput(std::string input){
    // Remove 'v4l2://' prefix to get the requested video device
    if (input.substr(0, 5) == "v4l2:"){
      input = input.substr(5);
    }
    // If /dev/ is not prepended to the input, add it
    if (input.substr(0, 5) != "/dev/"){
      input = "/dev/" + input;
    }
    return input;
  }
}// namespace Mist

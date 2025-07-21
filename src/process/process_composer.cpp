#include "../input/input.h"
#include "../output/output.h"
#include "../wpng/wpng_read.h"
#include "process.hpp"

#include <mist/pixels.h>
#include <mist/procs.h>
#include <mist/util.h>

#include <cmath>
#include <iostream>
#include <mutex>
#include <ostream>
#include <thread>

#ifdef HASQUIRC
#include "../quirc/decode.c"
#include "../quirc/identify.c"
#include "../quirc/quirc.c"
#include "../quirc/quirc.h"
#include "../quirc/version_db.c"
#endif

/// If true, running in input mode rather than process mode.
/// This means it'll auto-update its config from the stream config in real time.
bool inputMode = false;

enum TimingPos { TIMPOS_NO, TIMPOS_TL, TIMPOS_TM, TIMPOS_TR, TIMPOS_M };
TimingPos parseTiming(const std::string & o) {
  if (o == "no") { return TIMPOS_NO; }
  if (o == "topleft") { return TIMPOS_TL; }
  if (o == "topmid") { return TIMPOS_TM; }
  if (o == "topright") { return TIMPOS_TR; }
  if (o == "middle") { return TIMPOS_M; }
  return TIMPOS_NO;
}

enum OfflineAction { OFFACT_GREY, OFFACT_BLACK, OFFACT_KEEP, OFFACT_REMOVE };
OfflineAction parseOffAction(const std::string & o) {
  if (o == "grey") { return OFFACT_GREY; }
  if (o == "black") { return OFFACT_BLACK; }
  if (o == "keep") { return OFFACT_KEEP; }
  if (o == "remove") { return OFFACT_REMOVE; }
  return OFFACT_GREY;
}

Util::Config co;
std::string audioSource;
TimingPos doFrameTiming = TIMPOS_NO;
std::recursive_mutex drawMutex;
bool attemptSync = false;
uint64_t targetDelay = 0;

namespace Mist {

  JSON::Value opt; ///< Global configuration for process

  /// Source process, readies a PixFmtUYVY::SrcMatrix for thread-safe reading
  class ProcessSource : public Output {
    public:
      // Variables that will be read externally
      std::recursive_mutex *ptrLock = 0;
      bool shouldStop{false};
      uint64_t ptrTime{0};
      uint64_t maxVidTime{0}; ///< Newest video timestamp in unix millis available
      uint64_t minVidTime{0}; ///< Oldest video timestamp in unix millis available
      uint8_t utcSource{UTCSRC_UNKNOWN}; ///< Source of UTC timestamp
      bool syncLock{false}; ///< True if we're currently synced
      uint64_t maxAudTime{0};
      PixFmtUYVY::SrcMatrix ptr;
      size_t vidIdx{INVALID_TRACK_ID}; ///< Video track index

      size_t audIdx{INVALID_TRACK_ID}; ///< Audio track index
      Util::ResizeablePointer audBuffer; ///< Audio sample buffer
      uint64_t audTime{0}; ///< Timestamp in milliseconds of the beginning of the audio buffer.
      uint64_t audRate{0}; ///< Audio sample rate in Hz
      size_t audChannels{0}; ///< Audio channel count
      size_t audSize{0}; ///< Size of a single audio channel sample, in bits
      size_t sampBytes{0}; ///< Bytes per audio sample (all channels combined)
      size_t wholeMs{0}; ///< Samples needed (on each channel) to get an integer number of milliseconds

      uint64_t lastSelCheck;
      uint64_t lastRetime{0};

#ifdef HASQUIRC
      struct quirc *qrParser;
      uint64_t lastQrCheck{0};
#endif

      ProcessSource(Socket::Connection & c, Util::Config & cfg, JSON::Value & capa) : Output(c, cfg, capa) {
        lastSelCheck = Util::bootMS();
        closeMyConn();
        targetParams["keeptimes"] = true;
        realTime = 0;
        wantRequest = true;
        parseData = true;

#ifdef HASQUIRC
        qrParser = quirc_new();
        if (!qrParser) { WARN_MSG("QR decoder could not be loaded"); }
#endif
        initialize();
      }

      virtual ~ProcessSource() {
#ifdef HASQUIRC
        quirc_destroy(qrParser);
#endif
      }
      virtual void requestHandler(bool readable) {}
      bool isRecording() { return false; }
      bool isReadyForPlay() { return true; }
      inline virtual bool keepGoing() { return !shouldStop && Util::Config::is_active; }

      static void init(Util::Config *cfg, JSON::Value & capa) {
        Output::init(cfg, capa);
        capa["name"] = "Multiview";
        capa["codecs"][0u][0u].append("UYVY");
        capa["codecs"][0u][1u].append("PCM");
        cfg->addOption("streamname", R"({"arg":"string","short":"s","long":"stream"})");
        cfg->addBasicConnectorOptions(capa);
      }

      void connStats(uint64_t now, Comms::Connections & statComm) {
        // Ensure this process is ignored by the buffer to determine shutdown time
        for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++) {
          if (it->second) {
            it->second.setStatus(COMM_STATUS_DONOTTRACK | COMM_STATUS_NOKILL | it->second.getStatus());
          }
        }
      }

      virtual void invalidateTrackPage(size_t trackId) {
        if (!ptrLock) { return; }
        std::lock_guard<std::recursive_mutex> guardA(drawMutex);
        std::lock_guard<std::recursive_mutex> guardB(*ptrLock);
        ptr.pix = 0;
      }

      void sendNext() {
        if (shouldStop) { return; }
        if (lastSelCheck + 1000 < Util::bootMS()) {
          if (selectDefaultTracks()) {
            INFO_MSG("Updated track selection: now have %zu selected", userSelect.size());
          } else {
            VERYHIGH_MSG("Kept track selection: now have %zu selected", userSelect.size());
          }
          lastSelCheck = Util::bootMS();
        }
        needsLookAhead = 0;
        maxSkipAhead = 0;
        int64_t utcOffset = M.getUTCOffset();
        if (attemptSync && utcOffset) {
          if (thisIdx == vidIdx) {
            maxVidTime = M.getLastms(thisIdx) + utcOffset;
            minVidTime = M.getFirstms(thisIdx) + utcOffset;
            utcSource = M.getUTCSource();
            uint64_t currUnix = Util::unixMS();
            if (currUnix - targetDelay >= minVidTime + 50 && currUnix - targetDelay + 50 <= maxVidTime) {
              realTime = 1000;
              if (lastRetime + 3000 < thisBootMs) {
                lastRetime = thisBootMs;
                resetTiming(currUnix - utcOffset - targetDelay);
                syncLock = true;
              }
            } else {
              realTime = 0;
              syncLock = false;
            }
          }
        } else {
          maxVidTime = 0;
          minVidTime = 0;
          realTime = 0;
          syncLock = false;
        }

        // This prevents a string-based lookup after the first packets have arrived,
        // but still keeps track-reselection and switching functional with minimal overhead.
        if (thisIdx != vidIdx && thisIdx != audIdx) {
          if (M.getType(thisIdx) == "video") { vidIdx = thisIdx; }
          if (M.getType(thisIdx) == "audio") {
            audIdx = thisIdx;
            // Calculate bytes per sample
            audChannels = M.getChannels(thisIdx);
            audSize = M.getSize(thisIdx);
            sampBytes = audChannels * audSize / 8;
            // Calculate samples for an integer number of milliseconds
            audRate = M.getRate(thisIdx);
            if ((audRate / 1000) * 1000 == audRate) {
              wholeMs = audRate / 1000;
            } else if ((audRate / 100) * 100 == audRate) {
              wholeMs = audRate / 100;
            } else if (audRate == 11025 || audRate == 22050) {
              wholeMs = 441;
            }
          }
        }

        // Retrieve packet buffer pointers for video frame
        if (thisIdx == vidIdx) {
          std::lock_guard<std::recursive_mutex> guard(*ptrLock);
          ptrTime = M.packetTimeToUnixMs(thisTime);
          ptr.pix = (PixFmtUYVY::Pixels *)thisData;
          ptr.width = M.getWidth(thisIdx);
          ptr.height = M.getHeight(thisIdx);

#ifdef HASQUIRC
          if (qrParser && lastQrCheck + 5000 < thisTime) {
            lastQrCheck = thisTime;
            if (qrParser->w != ptr.width || qrParser->h != ptr.height) {
              if (quirc_resize(qrParser, ptr.width, ptr.height)) {
                WARN_MSG("Could not allocate space for QR decoder!");
              }
            }
            if (qrParser->w == ptr.width && qrParser->h == ptr.height) {
              uint8_t *image;
              int w, h;
              size_t i = 0;
              image = quirc_begin(qrParser, &w, &h);
              for (h = 0; h < ptr.height; ++h) {
                for (w = 0; w < ptr.width / 2; ++w) {
                  PixFmtUYVY::Pixels & P = ptr.pix[h * ptr.width / 2 + w];
                  image[i++] = P.y1;
                  image[i++] = P.y2;
                }
              }
              quirc_end(qrParser);
              size_t num_codes = quirc_count(qrParser);
              for (i = 0; i < num_codes; i++) {
                struct quirc_code code;
                struct quirc_data data;
                quirc_extract(qrParser, i, &code);
                quirc_decode_error_t err = quirc_decode(&code, &data);
                if (!err) {
                  std::string pl((char *)data.payload, data.payload_len);
                  if (pl.size() > 3 && pl.substr(0, 3) == "TC/") {
                    const std::string charset = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
                    uint64_t time = 0;
                    for (char c : pl.substr(3)) {
                      size_t value = charset.find(c);
                      if (value == std::string::npos) { return; }
                      time = time * 45 + value;
                    }
                    int64_t uOff = time - thisTime;
                    int64_t oldOff = M.getUTCOffset();
                    meta.setUTCOffset(uOff, UTCSRC_QRSYNCED);
                    utcSource = UTCSRC_QRSYNCED;
                    INFO_MSG("QR %zu/%zu: UTC offset set from %" PRId64 " to %" PRId64, i + 1, num_codes, oldOff, uOff);
                  } else {
                    INFO_MSG("QR %zu/%zu: Data: %s", i + 1, num_codes, data.payload);
                  }
                }
              }
            }
          }
#endif
          return;
        }

        if (thisIdx == audIdx) {
          uint64_t bmsTime = thisTime + M.getBootMsOffset() - (attemptSync ? targetDelay : 0);
          std::lock_guard<std::recursive_mutex> guard(*ptrLock);
          maxAudTime = M.packetTimeToUnixMs(M.getLastms(thisIdx));
          if (!audBuffer.size()) {
            audTime = bmsTime;
          } else if (audTime + (audBuffer.size() / sampBytes) * 1000 / audRate < bmsTime) {
            // Insert zeroed out audio data for the missing section
            size_t insertWhite = ((bmsTime - audTime) * audRate / 1000) * sampBytes - audBuffer.size();
            audBuffer.appendNull(insertWhite);
            INFO_MSG("Inserted %zu bytes of missing audio data", insertWhite);
          }
          audBuffer.append(thisData, thisDataLen);
          return;
        }
      }
  };

  /// Helper class to aid in (re)starting the ProcessSource classes
  class Source {
    public:
      std::thread *T{0};
      Util::Config C;
      ProcessSource *P{0};
      std::recursive_mutex ptrLock;
      const std::string S;
      bool ready{false};
      bool shouldStop{false};
      bool harvestable{false};
      wpng_load_output staticImg{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
      uint64_t lastImg{0};

      Source(const std::string & src) : S(src) {}
      ~Source() {
        if (T) {
          stop();
          T->join();
          delete T;
        }
        if (P) { delete P; }
        if (staticImg.data) {
          free(staticImg.data);
          staticImg.data = 0;
        }
      }
      void stop() {
        shouldStop = true;
        std::lock_guard<std::recursive_mutex> guard(ptrLock);
        if (P) { P->shouldStop = true; }
      }
      void run() {
        if (T) { return; }
        // Check if it ends in .png and is an existing file
        if (S.size() > 4 && S.substr(S.size() - 4) == ".png") {
          struct stat statRslt;
          if (!stat(S.c_str(), &statRslt) && (statRslt.st_mode & S_IFMT) == S_IFREG) {
            T = new std::thread([&]() {
              Util::ResizeablePointer fileData;
              Util::nameThread(S);
              Util::setStreamName(S + "→" + opt["sink"].asStringRef());
              INFO_MSG("Started PNG image source thread for: %s...", S.c_str());
              uint64_t lastRead = 0;
              while (!shouldStop && Util::Config::is_active) {
                struct stat statRslt;
                if (!stat(S.c_str(), &statRslt) && statRslt.st_mtim.tv_sec > lastRead) {
                  std::lock_guard<std::recursive_mutex> guard(ptrLock);
                  if (staticImg.data) {
                    free(staticImg.data);
                    staticImg.data = 0;
                  }
                  std::ifstream inFile(S);
                  fileData.truncate(0);
                  fileData.allocate(statRslt.st_size);
                  fileData.append(0, statRslt.st_size);
                  inFile.read(fileData, statRslt.st_size);
                  byte_buffer in_buf = {(uint8_t *)(char *)fileData, (size_t)statRslt.st_size, (size_t)statRslt.st_size, 0};
                  wpng_load(&in_buf, WPNG_READ_SKIP_CRC | WPNG_READ_SKIP_CRITICAL_CHUNKS | WPNG_READ_SKIP_IDAT_CRC | WPNG_READ_FORCE_8BIT,
                            &staticImg);
                  ready = !staticImg.error;
                  if (ready) { lastImg = Util::bootMS(); }
                  lastRead = statRslt.st_mtim.tv_sec;
                }
                if (!shouldStop && Util::Config::is_active) { Util::sleep(1000); }
              }
              {
                std::lock_guard<std::recursive_mutex> guard(ptrLock);
                if (staticImg.data) {
                  free(staticImg.data);
                  staticImg.data = 0;
                }
              }
              INFO_MSG("Stopping source thread for %s...", S.c_str());
              harvestable = true;
            });
            return;
          }
        }
        T = new std::thread([&]() {
          Util::nameThread(S);
          JSON::Value capa;
          Mist::ProcessSource::init(&C, capa);
          C.getOption("streamname", true).append(S);
          C.addOption("target", R"({"arg":"string", "default":"", "arg_num":1})");
          C.addOption("noinput", R"({"default":true})");
          C.getOption("target", true).append("-");
          Util::setStreamName(S + "→" + opt["sink"].asStringRef());
          INFO_MSG("Started source thread for: %s...", S.c_str());
          while (!shouldStop && Util::Config::is_active) {
            uint8_t status = Util::getStreamStatus(S);
            if (status != STRMSTAT_READY) {
              Util::sleep(500);
              continue;
            }
            {
              std::lock_guard<std::recursive_mutex> guard(ptrLock);
              Socket::Connection sock(1, 0);
              P = new Mist::ProcessSource(sock, C, capa);
              P->ptrLock = &ptrLock;
              ready = true;
              Util::setStreamName(S + "→" + opt["sink"].asStringRef());
            }
            P->run();
            {
              std::lock_guard<std::recursive_mutex> guardA(drawMutex);
              std::lock_guard<std::recursive_mutex> guardB(ptrLock);
              ready = false;
              delete P;
              P = 0;
            }
            if (!shouldStop && Util::Config::is_active) {
              Util::sleep(500);
              INFO_MSG("Restarting source thread for %s...", S.c_str());
            }
          }
          INFO_MSG("Stopping source thread for %s...", S.c_str());
          harvestable = true;
        });
      }

      // Move constructor
      Source(Source && a) : S(a.S) {
        ready = a.ready;
        T = a.T;
        a.T = 0;
        C = a.C;
        P = a.P;
        a.P = 0;
      }
      // Remove assignment and copy constructors
      Source(const Source &) = delete;
      Source & operator=(const Source &) = delete;
  };

  class Destination : public PixFmtUYVY::DestMatrix {
    public:
      std::string strmName; ///< Source name
      std::string label; ///< Source label
      uint64_t ptrTimeCpy; ///< Time of last draw
      std::set<size_t> damages; ///< Which other destinations are damaged by this destination
      inline void dmg() { ptrTimeCpy = 0; }
      inline bool overlaps(const DestMatrix & rhs) {
        return (cellX < rhs.cellX + rhs.cellWidth) && (cellX + cellWidth > rhs.cellX) &&
          (cellY < rhs.cellY + rhs.cellHeight) && (cellY + cellHeight > rhs.cellY);
      }
      OfflineAction offlineAction;
      size_t removeTimeout{15000};
      TimingPos frameTiming{TIMPOS_NO};
      size_t savedWidth{0};
      size_t savedHeight{0};
      Util::ResizeablePointer savedPixels;
      bool isBlack{true};
      uint64_t blackSince{0};
      bool isRemoved{false};

      bool unavailable(size_t i, std::vector<Destination> & cells) {
        if (!isBlack) {
          // Blacken according to offline action
          switch (offlineAction) {
            case OFFACT_GREY: greyscale(); break;
            case OFFACT_BLACK: blacken(); break;
            case OFFACT_REMOVE:
              // Overdraw with black, then damage lower surfaces to trigger a redraw next frame
              blacken();
              for (auto & C : cells) {
                if (C.damages.count(i)) { C.dmg(); }
              }
              break;
            case OFFACT_KEEP: break;
          }
          isBlack = true;
          blackSince = Util::bootMS();
          for (auto & N : damages) { cells[N].dmg(); }
          isRemoved = offlineAction == OFFACT_REMOVE;
          if (!isRemoved) {
            // Save (potentially changed) pixels as-is for potential redraws
            savedWidth = cellWidth - blankL - blankR;
            savedHeight = cellHeight - blankT - blankB;
            savedPixels.truncate(0);
            size_t pxSize = Util::pixfmtToSize("UYVY", savedWidth, savedHeight);
            savedPixels.allocate(pxSize);
            savedPixels.append(0, pxSize);
            blitToPtr((PixFmtUYVY::Pixels *)(char *)savedPixels);
          }
          PixFmtUYVY::writeText(*this, label);
          return true;
        }
        if (offlineAction == OFFACT_REMOVE || isRemoved) { return false; }
        if (!isRemoved && removeTimeout && blackSince + removeTimeout < Util::bootMS()) {
          // Overdraw with black, then damage lower surfaces to trigger a redraw next frame
          blacken();
          for (auto & C : cells) {
            if (C.damages.count(i)) { C.dmg(); }
          }
          isRemoved = true;
          return true;
        }
        if (!ptrTimeCpy && savedPixels.size() && savedWidth && savedHeight) {
          // Trigger a redraw of the old pixels)
          blitFromPtr((PixFmtUYVY::Pixels *)(char *)savedPixels);
          PixFmtUYVY::writeText(*this, label);
          ptrTimeCpy = 1; // Mark as drawn (any non-zero value will do)
          return true;
        }
        return false;
      }
  };

  std::map<std::string, Source> sources;
  std::vector<Destination> cells;
  JSON::Value prevSources;

  /// Writes out the actual composed UYVY data by composing from the sources
  class ProcessSink : public Input {
    public:
      size_t vidIdx{INVALID_TRACK_ID}, audIdx{INVALID_TRACK_ID};
      size_t audBytes{0};
      size_t audMs{0};
      size_t audLastMs{0};
      size_t vidLastMs{0};
      size_t audioChannels{0};
      size_t audioSampSize{0};
      size_t audioSampRate{0};

      ProcessSink(Util::Config *cfg) : Input(cfg) {
        capa["name"] = "AV";
        streamName = opt["sink"].asString();
        Util::streamVariables(streamName, "");
        Util::setStreamName(streamName);
        if (opt.isMember("target_mask") && !opt["target_mask"].isNull() && opt["target_mask"].asString() != "") {
          DTSC::trackValidDefault = opt["target_mask"].asInt();
        }
      }

      ~ProcessSink() {}
      bool checkArguments() { return true; }
      bool needHeader() { return false; }
      bool readHeader() { return true; }
      bool openStreamSource() { return true; }
      void parseStreamHeader() {}
      bool needsLock() { return false; }
      bool isSingular() { return false; }
      virtual bool publishesTracks() { return false; }
      void connStats(Comms::Connections & statComm) {}

      void streamMainLoop() {
        uint64_t statTimer = 0;
        uint64_t startTime = Util::bootSecs();
#define DELAY_COUNT 200
        size_t delaySeen[DELAY_COUNT];
        memset(delaySeen, 0, sizeof(delaySeen)); // null all delays so far
        size_t delayCounter{0};
        thisIdx = INVALID_TRACK_ID;
        Comms::Connections statComm;

        Util::ResizeablePointer outPtr;

        size_t outputBufferSize = 0;
        // Parse output resolution from config
        size_t outputWidth = 0;
        size_t outputHeight = 0;
        if (Mist::opt.isMember("resolution") && Mist::opt["resolution"].isString()) {
          std::string res = Mist::opt["resolution"].asString();
          size_t xPos = res.find('x');
          if (xPos != std::string::npos) {
            outputWidth = strtoul(res.substr(0, xPos).c_str(), NULL, 10);
            outputHeight = strtoul(res.substr(xPos + 1).c_str(), NULL, 10);
          }
        } else {
          outputWidth = 1920;
          outputHeight = 1080;
        }
        // Ensure output dimensions are even (required for UYVY)
        outputWidth &= ~1;
        outputHeight &= ~1;
        // Calculate byte size of buffer
        outputBufferSize = outputWidth * outputHeight * 2;

        // Allocate output buffer if needed
        if (outPtr.size() != outputBufferSize) {
          outPtr.truncate(0);
          outPtr.allocate(outputBufferSize);
          INFO_MSG("Output buffer size: %zu bytes (output resolution: %zux%zu)", outputBufferSize, outputWidth, outputHeight);

          // Clear the buffer to black
          PixFmtUYVY::Pixels *outPixels = (PixFmtUYVY::Pixels *)(void *)outPtr;
          size_t numPixelPairs = outputBufferSize / 4;
          for (size_t i = 0; i < numPixelPairs; i++) { outPixels[i].clear(); }
          outPtr.append(0, outputBufferSize);

          if (inputMode && meta.getValidTracks().size()) {
            Util::Config::is_active = false;
            Util::logExitReason(ER_CLEAN_EOF, "We're in input mode but tracks already exist; aborting.");
            return;
          }

          thisIdx = vidIdx = meta.addTrack(0, 0, 0, 0, true, outputBufferSize);
          meta.setType(thisIdx, "video");
          meta.setCodec(thisIdx, "UYVY");
          meta.setWidth(thisIdx, outputWidth);
          meta.setHeight(thisIdx, outputHeight);
          meta.setID(thisIdx, thisIdx);
          meta.markUpdated(thisIdx);
        }

        // Parse target framerate from config
        uint32_t targetFPS = 0;
        uint64_t frameIntervalMs = 0;
        if (Mist::opt.isMember("target_fps") && Mist::opt["target_fps"].isInt()) {
          targetFPS = Mist::opt["target_fps"].asInt();
          frameIntervalMs = 1000 / targetFPS;
          INFO_MSG("Target framerate: %u fps (%.1f ms per frame)", targetFPS, (float)frameIntervalMs);
          meta.setFpks(thisIdx, targetFPS * 1000);
        } else {
          INFO_MSG("No target framerate set - will wait for all sources to have fresh frames");
        }

        PixFmt::scaleMode scaling = PixFmt::INTEGER;
        if (Mist::opt.isMember("scaling") && Mist::opt["scaling"].isString()) {
          scaling = PixFmt::parseScaling(Mist::opt["scaling"].asStringRef());
        }

        PixFmt::aspectMode aspect = PixFmt::LETTERBOX;
        if (Mist::opt.isMember("aspect") && Mist::opt["aspect"].isString()) {
          aspect = PixFmt::parseAspect(Mist::opt["aspect"].asStringRef());
          if (scaling == PixFmt::INTEGER && aspect == PixFmt::STRETCH) {
            WARN_MSG("Integer scaling cannot stretch - cropping instead");
            aspect = PixFmt::CROP;
          }
        }

        size_t removeTimeout = Mist::opt.isMember("removetimeout") ? Mist::opt["removetimeout"].asInt() : 15000;
        OfflineAction offlineAction = OFFACT_GREY;
        if (Mist::opt.isMember("offlineaction")) {
          offlineAction = parseOffAction(Mist::opt["offlineaction"].asStringRef());
        }

        std::function<bool(bool)> startSources = [&](bool forceRecalc) {
          std::set<std::string> srcStrings;
          size_t gridCount = 0; ///< Count how many streams use grid positioning
          if (audioSource.size()) {
            srcStrings.insert(audioSource);
            if (!sources.count(audioSource)) {
              auto placed = sources.emplace(audioSource, audioSource);
              placed.first->second.run();
            }
          }
          jsonForEach (Mist::opt["source"], it) {
            // Normalize the source config
            if (it->isString() && it->asStringRef().size() && *(it->asStringRef().begin()) == '{') {
              *it = JSON::fromString(it->asString());
            }

            // Start an output thread for each unique stream name
            std::string strmName;
            if (it->isObject()) {
              strmName = it->isMember("stream") ? (*it)["stream"].asStringRef() : "";
              if (!it->isMember("x") || !it->isMember("y") || !it->isMember("w") || !it->isMember("h")) { ++gridCount; }
            } else {
              strmName = it->asStringRef();
              ++gridCount;
            }
            srcStrings.insert(strmName);
            if (strmName.size() && !sources.count(strmName)) {
              auto placed = sources.emplace(strmName, strmName);
              placed.first->second.run();
            }
          }
          // Find output threads that need to be stopped or are stopped
          std::set<std::string> toWipe;
          for (auto & it : sources) {
            if (!srcStrings.count(it.first)) { it.second.stop(); }
            if (it.second.harvestable) { toWipe.insert(it.first); }
          }
          // Wipe the stopped ones (we could insta-wipe them, but it might block if we did)
          for (const std::string & wipe : toWipe) { sources.erase(wipe); }

          if (forceRecalc || Mist::opt["source"] != prevSources) {
            prevSources = Mist::opt["source"];
            size_t srcCount = Mist::opt["source"].size();
            cells.resize(srcCount);

            if (!gridCount) { gridCount = 1; }
            size_t cols = std::ceil(std::sqrt(gridCount));
            size_t rows = std::ceil((double)gridCount / cols);
            size_t cellWidth = (outputWidth / cols) & ~1; // Make even
            size_t cellHeight = outputHeight / rows;
            size_t gridNo = 0;
            jsonForEach (Mist::opt["source"], it) {
              size_t N = it.num();
              Destination & c = cells[N];
              c.pix = (PixFmtUYVY::Pixels *)(char *)outPtr;
              c.totHeight = outputHeight;
              c.totWidth = outputWidth;
              c.blankL = c.blankR = c.blankT = c.blankB = 0;
              c.ptrTimeCpy = 0;
              c.isBlack = true;
              c.scale = scaling;
              c.aspect = aspect;
              c.removeTimeout = removeTimeout;
              c.offlineAction = offlineAction;
              c.frameTiming = doFrameTiming;

              if (it->isObject()) {
                c.strmName = it->isMember("stream") ? (*it)["stream"].asStringRef() : "";
                // Override text label if set
                if (!it->isMember("text") || !(*it)["text"].isString()) {
                  c.label = c.strmName;
                } else {
                  c.label = (*it)["text"].asStringRef();
                }
                if (it->isMember("aspect") && (*it)["aspect"].isString() && (*it)["aspect"].asStringRef().size()) {
                  c.aspect = PixFmt::parseAspect((*it)["aspect"].asStringRef());
                }
                if (it->isMember("scaling") && (*it)["scaling"].isString() && (*it)["scaling"].asStringRef().size()) {
                  c.scale = PixFmt::parseScaling((*it)["scaling"].asStringRef());
                }
                if (it->isMember("removetimeout") && (*it)["removetimeout"].isInt()) {
                  c.removeTimeout = (*it)["removetimeout"].asInt();
                }
                if (it->isMember("offlineaction") && (*it)["offlineaction"].isString()) {
                  c.offlineAction = parseOffAction((*it)["offlineaction"].asStringRef());
                }
                if (it->isMember("printtiming")) {
                  if ((*it)["printtiming"].isString()) {
                    c.frameTiming = parseTiming((*it)["printtiming"].asStringRef());
                  } else if ((*it)["printtiming"].asBool()) {
                    c.frameTiming = TIMPOS_TL;
                  }
                }
              } else {
                c.strmName = it->asStringRef();
              }

              if (it->isObject() && it->isMember("x") && it->isMember("y") && it->isMember("w") && it->isMember("h")) {
                // Manual positioning
                c.cellWidth = (*it)["w"].asInt() & ~1;
                c.cellHeight = (*it)["h"].asInt();
                c.cellX = (*it)["x"].asInt() & ~1;
                c.cellY = (*it)["y"].asInt();
              } else {
                // Calculate based on a grid
                c.cellWidth = cellWidth;
                c.cellHeight = cellHeight;
                c.cellX = (gridNo % cols) * cellWidth;
                c.cellY = (gridNo / cols) * cellHeight;
                ++gridNo;
              }

              // Calculate overlap, ensure lower images damage the ones above them
              size_t i = -1;
              for (auto & dest : cells) {
                ++i;
                if (i == N) { break; }
                if (c.overlaps(dest)) { dest.damages.insert(N); }
              }
            }

            return true;
          }
          return false;
        };

        // Calculate grid layout
        startSources(true);
        bool makeBlack = false;

        thisTime = 0;
        uint64_t lastRefresh = Util::bootMS();
        while (Util::Config::is_active) {
          uint64_t nowMs = Util::bootMS();

          // Check if we need to shut down
          if (!userSelect.count(vidIdx)) {
            userSelect[vidIdx].reload(streamName, vidIdx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
          }
          if (userSelect[vidIdx].getStatus() & COMM_STATUS_REQDISCONNECT) {
            Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
            Util::Config::is_active = false;
            return;
          }
          if (isSingular() && !bufferActive()) {
            Util::logExitReason(ER_SHM_LOST, "Buffer shut down");
            return;
          }

          if (lastRefresh + 1000 < nowMs) {
            std::string strName = streamName;
            Util::sanitizeName(strName);
            strName = strName.substr(0, strName.find_first_of("+ "));
            char tmpBuf[NAME_BUFFER_SIZE];
            snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_STREAM_CONF, strName.c_str());
            Util::DTSCShmReader rStrmConf(tmpBuf);
            DTSC::Scan streamCfg = rStrmConf.getScan();

            if (streamCfg) {
              // Update audio source
              if (streamCfg.getMember("copyaudio")) {
                std::string newAud = streamCfg.getMember("copyaudio").asString();
                if (newAud != audioSource) {
                  INFO_MSG("Changing audio source from %s to %s", audioSource.c_str(), newAud.c_str());
                  audioSource = newAud;
                }
              }
              {
                TimingPos newTime = TIMPOS_NO;
                if (streamCfg.getMember("printtiming")) {
                  if (streamCfg.getMember("printtiming").isString()) {
                    newTime = parseTiming(streamCfg.getMember("printtiming").asString());
                  } else if (streamCfg.getMember("printtiming").asBool()) {
                    newTime = TIMPOS_TL;
                  }
                }
                if (newTime != doFrameTiming) {
                  doFrameTiming = newTime;
                  makeBlack = true;
                }
              }
              {
                bool newSync = streamCfg.getMember("sourcesync") && streamCfg.getMember("sourcesync").asBool();
                if (newSync != attemptSync) {
                  INFO_MSG("Changing frame sync from %s to %s", attemptSync ? "on" : "off", newSync ? "on" : "off");
                  attemptSync = newSync;
                }
              }
              // Update default scaling mode
              PixFmt::scaleMode newScale = PixFmt::INTEGER;
              if (streamCfg.getMember("scaling")) {
                newScale = PixFmt::parseScaling(streamCfg.getMember("scaling").asString());
              }
              if (newScale != scaling) {
                INFO_MSG("Updating default scaling mode");
                scaling = newScale;
                makeBlack = true;
              }
              // Update default aspect mode
              PixFmt::aspectMode newAspect = PixFmt::LETTERBOX;
              if (streamCfg.getMember("aspect")) {
                newAspect = PixFmt::parseAspect(streamCfg.getMember("aspect").asString());
              }
              if (scaling == PixFmt::INTEGER && newAspect == PixFmt::STRETCH) {
                WARN_MSG("Integer scaling cannot stretch - cropping instead");
                newAspect = PixFmt::CROP;
              }
              if (newAspect != aspect) {
                aspect = newAspect;
                makeBlack = true;
              }
              // Update offline action
              if (streamCfg.getMember("offlineaction")) {
                OfflineAction newAct = parseOffAction(streamCfg.getMember("offlineaction").asString());
                if (newAct != offlineAction) {
                  offlineAction = newAct;
                  makeBlack = true;
                }
              }
              // Update remove timeout
              if (streamCfg.getMember("removetimeout")) {
                size_t newRT = streamCfg.getMember("removetimeout").asInt();
                if (newRT != removeTimeout) {
                  removeTimeout = newRT;
                  makeBlack = true;
                }
              }
              // Update target frame rate
              if (streamCfg.getMember("target_fps")) {
                targetFPS = streamCfg.getMember("target_fps").asInt();
                frameIntervalMs = 1000 / targetFPS;
                meta.setFpks(vidIdx, targetFPS * 1000);
              } else {
                targetFPS = 0;
                frameIntervalMs = 0;
                meta.setFpks(vidIdx, 0);
              }
              // Reconfigure sources
              if (streamCfg.getMember("sources")) {
                Mist::opt["source"] = streamCfg.getMember("sources").asJSON();
                makeBlack |= startSources(makeBlack);
              }
            }
            lastRefresh = nowMs;
          }

          bool changed = false;
          { // Scope for drawMutex
            std::lock_guard<std::recursive_mutex> guard(drawMutex);

            // Back up pixels if needed
            size_t i = -1;
            for (auto & dest : cells) {
              ++i;
              if (!dest.strmName.size()) { continue; } // No stream name set for this source, skip
              auto strm = sources.find(dest.strmName);
              if (strm == sources.end()) { continue; } // Stream not loaded (yet), skip
              Source & s = strm->second;

              // Skip if source is not ready / available
              if (!s.ready) {
                if (dest.unavailable(i, cells)) {
                  for (auto & N : dest.damages) { cells[N].dmg(); }
                  changed = true;
                }
                continue;
              }

              {
                std::lock_guard<std::recursive_mutex> guard(s.ptrLock);
                if (!s.P && s.staticImg.data && s.staticImg.size) { continue; }
                if (!s.P || !s.P->ptr.width || !s.P->ptr.height || !s.P->ptr.pix) {
                  if (dest.unavailable(i, cells)) {
                    for (auto & N : dest.damages) { cells[N].dmg(); }
                    changed = true;
                  }
                }
              }
            }

            if (makeBlack) {
              changed = true;
              makeBlack = false;

              // Clear the buffer to black
              PixFmtUYVY::Pixels *outPixels = (PixFmtUYVY::Pixels *)(void *)outPtr;
              size_t numPixelPairs = outputBufferSize / 4;
              for (size_t i = 0; i < numPixelPairs; i++) { outPixels[i].clear(); }
              // Damage all sources, causing them to redraw
              for (auto & dest : cells) { dest.dmg(); }
            }

            uint64_t minTime = Util::unixMS();
            uint64_t currUnix = minTime;

            if (audioSource.size()) {
              // Check for audio
              auto strm = sources.find(audioSource);
              if (strm != sources.end()) {
                Source & s = strm->second;
                if (s.ready) {
                  std::lock_guard<std::recursive_mutex> guard(s.ptrLock);

                  if (s.P && s.P->maxAudTime && minTime > s.P->maxAudTime) { minTime = s.P->maxAudTime; }

                  if (s.P && s.P->audBuffer.size() && s.P->wholeMs && s.P->sampBytes) {
                    audBytes = (s.P->wholeMs * s.P->sampBytes);
                    audMs = 1000 * s.P->wholeMs / s.P->audRate;
                    size_t blocks = s.P->audBuffer.size() / audBytes;
                    if (blocks) {
                      size_t bytes = audBytes * blocks;
                      if (audIdx == INVALID_TRACK_ID) {
                        INFO_MSG("Adding audio track!");
                        thisIdx = audIdx = meta.addTrack();
                        audioChannels = s.P->audChannels;
                        audioSampRate = s.P->audRate;
                        audioSampSize = s.P->audSize;
                        if (Mist::opt.isMember("audiochannels") && Mist::opt["audiochannels"].isInt()) {
                          audioChannels = Mist::opt["audiochannels"].asInt();
                        }
                        meta.setType(thisIdx, "audio");
                        meta.setCodec(thisIdx, "PCM");
                        meta.setRate(thisIdx, audioSampRate);
                        meta.setSize(thisIdx, audioSampSize);
                        meta.setChannels(thisIdx, audioChannels);
                        meta.setID(thisIdx, thisIdx);
                        meta.markUpdated(thisIdx);
                      }
                      if (s.P->audTime >= audLastMs) {
                        if (s.P->audTime > audLastMs && audLastMs) { audForwardTo(s.P->audTime); }
                        thisTime = s.P->audTime;
                        thisIdx = audIdx;
                        bufferLivePacket(thisTime, 0, audIdx, s.P->audBuffer, bytes, 0, true);
                        s.P->audBuffer.shift(bytes);
                        audLastMs = s.P->audTime = thisTime + blocks * audMs;
                      } else {
                        // Wipe buffer if too far behind
                        s.P->audBuffer.truncate(0);
                      }
                    }
                  }
                }
              }
            }
            if (audIdx != INVALID_TRACK_ID && audLastMs && audLastMs + targetDelay + 250 < nowMs) {
              audForwardTo(nowMs - targetDelay - 250);
            }

            i = -1;
            for (auto & dest : cells) {
              ++i;

              if (!dest.strmName.size()) { continue; } // No stream name set for this source, skip
              auto strm = sources.find(dest.strmName);
              if (strm == sources.end()) { continue; } // Stream not loaded (yet), skip
              Source & s = strm->second;

              // Skip if source is not ready / available
              if (!s.ready) {
                if (dest.unavailable(i, cells)) {
                  for (auto & N : dest.damages) { cells[N].dmg(); }
                  changed = true;
                }
                continue;
              }
              {
                std::lock_guard<std::recursive_mutex> guard(s.ptrLock);

                bool didCopy = false;

                if (!s.P && s.staticImg.data && s.staticImg.size) {
                  if (dest.ptrTimeCpy != s.lastImg) {
                    dest.ptrTimeCpy = s.lastImg;
                    changed = true;
                    switch (s.staticImg.bytes_per_pixel) {
                      case 1: {
                        WARN_MSG("Copy PNG: Y not implemented");
                        PixFmtY::SrcMatrix src(s.staticImg.data, s.staticImg.width, s.staticImg.height);
                        PixFmt::copyScaled(src, dest);
                        break;
                      }
                      case 2: {
                        WARN_MSG("Copy PNG: YA not implemented");
                        PixFmtYA::SrcMatrix src(s.staticImg.data, s.staticImg.width, s.staticImg.height);
                        PixFmt::copyScaled(src, dest);
                        break;
                      }
                      case 3: {
                        WARN_MSG("Copy PNG: RGB not implemented");
                        PixFmtRGB::SrcMatrix src(s.staticImg.data, s.staticImg.width, s.staticImg.height);
                        PixFmt::copyScaled(src, dest);
                        break;
                      }
                      case 4: {
                        PixFmtRGBA::SrcMatrix src(s.staticImg.data, s.staticImg.width, s.staticImg.height);
                        PixFmt::copyScaled(src, dest);
                        break;
                      }
                    }
                    didCopy = true;
                  }
                } else if (!s.P || !s.P->ptr.width || !s.P->ptr.height || !s.P->ptr.pix) {
                  if (dest.unavailable(i, cells)) {
                    changed = true;
                    didCopy = true;
                  }
                } else {
                  if (s.P->maxVidTime && s.P->maxVidTime < minTime) { minTime = s.P->maxVidTime; }
                  if (dest.ptrTimeCpy != s.P->ptrTime) {
                    if (dest.isBlack) {
                      // Damage lower surfaces to trigger a redraw next frame
                      for (auto & C : cells) {
                        if (C.damages.count(i)) { C.dmg(); }
                      }
                      // But we should also draw over with black, in case the new and old data don't
                      // cover the same area and there is nothing behind it that will draw over it.
                      dest.blacken();
                      dest.isBlack = false;
                    }
                    dest.ptrTimeCpy = s.P->ptrTime;
                    PixFmtUYVY::copyScaled(s.P->ptr, dest);
                    changed = true;
                    didCopy = true;
                  }
                }
                if (didCopy) {
                  for (auto & N : dest.damages) { cells[N].dmg(); }
                  PixFmtUYVY::writeText(dest, dest.label);

                  if (dest.frameTiming != TIMPOS_NO) {
                    size_t lines = 1;
                    if (s.P && s.P->ptrTime) { ++lines; }
                    if (s.P && s.P->maxVidTime && s.P->minVidTime && attemptSync) { ++lines; }

                    size_t startX = dest.cellX + 4 + dest.blankL;
                    size_t startY = dest.cellY + 4 + dest.blankT;
                    if (dest.frameTiming == TIMPOS_M) { startY = dest.cellY + dest.cellHeight / 2 - lines * 4; }
                    if (dest.frameTiming == TIMPOS_TM) { startX = dest.cellX + dest.cellWidth / 2; }
                    if (dest.frameTiming == TIMPOS_TR) { startX = dest.cellX + dest.cellWidth - dest.blankR - 4; }

                    {
                      std::string txt = "Draw time: " + Util::getUTCStringMillis();
                      size_t X = startX;
                      size_t Y = startY;
                      size_t p = 0;
                      size_t offset = 0;
                      size_t len = PixFmtUYVY::utf8Len(txt);
                      if (dest.frameTiming == TIMPOS_TR) { X -= len * 8; }
                      if (dest.frameTiming == TIMPOS_TM) { X -= len * 4; }
                      while (p < len && offset < txt.size()) {
                        if (writeCodePoint(txt, offset, dest, X + p * 8, Y)) { ++p; }
                      }
                    }
                    if (s.P && s.P->ptrTime) {
                      std::string txt = "Frame time: " + Util::getUTCStringMillis(s.P->ptrTime);
                      switch (s.P->utcSource) {
                        case UTCSRC_QRSYNCED: txt += " (synced by QR code)"; break;
                        case UTCSRC_PROTOCOL: txt += " (protocol-based)"; break;
                        default: txt += " (guess)"; break;
                      }
                      size_t X = startX;
                      size_t Y = startY + 8;
                      size_t p = 0;
                      size_t offset = 0;
                      size_t len = PixFmtUYVY::utf8Len(txt);
                      if (dest.frameTiming == TIMPOS_TR) { X -= len * 8; }
                      if (dest.frameTiming == TIMPOS_TM) { X -= len * 4; }
                      while (p < len && offset < txt.size()) {
                        if (writeCodePoint(txt, offset, dest, X + p * 8, Y)) { ++p; }
                      }
                    }
                    if (s.P && s.P->maxVidTime && s.P->minVidTime && attemptSync) {
                      uint64_t targetTime = currUnix - targetDelay;
                      std::string txt = "Target: " + std::to_string(targetDelay) + " ms ago; ";
                      if (targetTime + 50 < s.P->minVidTime) {
                        txt += std::to_string(s.P->minVidTime - targetTime) + "ms too far in past";
                      } else if (targetTime > s.P->maxVidTime + 50) {
                        txt += std::to_string(targetTime - s.P->minVidTime) + "ms too far in future";
                      } else {
                        if (s.P->syncLock) {
                          txt += "synced";
                        } else {
                          txt += "syncing...";
                        }
                      }
                      size_t X = startX;
                      size_t Y = startY + 16;
                      size_t p = 0;
                      size_t offset = 0;
                      size_t len = PixFmtUYVY::utf8Len(txt);
                      if (dest.frameTiming == TIMPOS_TR) { X -= len * 8; }
                      if (dest.frameTiming == TIMPOS_TM) { X -= len * 4; }
                      while (p < len && offset < txt.size()) {
                        if (writeCodePoint(txt, offset, dest, X + p * 8, Y)) { ++p; }
                      }
                    }
                  }
                }
              }
            }

            if (attemptSync) {
              if (currUnix > minTime) {
                uint64_t nowDelay = currUnix - minTime;
                delaySeen[(++delayCounter) % DELAY_COUNT] = nowDelay;
                if (nowDelay > targetDelay) {
                  HIGH_MSG("Increasing target delay from %" PRIu64 " to %" PRIu64, targetDelay, nowDelay);
                  targetDelay = nowDelay;
                } else if (!(delayCounter % 10) && nowDelay < targetDelay) {
                  size_t totDelay = 0;
                  size_t maxDelay = 0;
                  for (int i = 0; i < DELAY_COUNT; ++i) {
                    totDelay += delaySeen[i];
                    if (delaySeen[i] > maxDelay) { maxDelay = delaySeen[i]; }
                  }
                  size_t weighted = (totDelay / DELAY_COUNT + maxDelay * 2) / 3 + 1;
                  uint64_t newDelay = (targetDelay + weighted) / 2;
                  if (newDelay + 10 < targetDelay) {
                    HIGH_MSG("Lowering target delay from %" PRIu64 " to %" PRIu64, targetDelay, newDelay);
                    targetDelay = newDelay;
                  }
                }
              }
            } else {
              targetDelay = 0;
            }
          } // Scope for drawMutex

          if (!targetFPS && !changed && nowMs < vidLastMs + 500) {
            Util::sleep(10);
            continue;
          }

          thisTime = vidLastMs = nowMs;
          thisIdx = vidIdx;
          bufferLivePacket(thisTime, 0, vidIdx, outPtr, outPtr.size(), 0, true);

          if (Util::bootSecs() - statTimer > 1) {
            // Connect to stats for INPUT detection
            if (!statComm && !getenv("NOSESS")) {
              statComm.reload(streamName, getConnectedBinHost(), JSON::Value(getpid()).asString(),
                              "INPUT:" + capa["name"].asStringRef(), "");
            }
            if (statComm) {
              if (!statComm) {
                Util::Config::is_active = false;
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
          }
          if (targetFPS) { Util::sleep(frameIntervalMs); }
        }
      }

      /// Inserts null bytes to get towards the timestamp listed (non-inclusive)
      void audForwardTo(uint64_t targetTime) {
        static Util::ResizeablePointer audNull;
        size_t blocks = (targetTime - audLastMs) / audMs;
        if (!blocks) { return; }
        HIGH_MSG("Inserting %zums empty audio data because source is too far behind", (size_t)(targetTime - audLastMs));
        if (audNull.size() < audBytes * blocks) { audNull.appendNull((audBytes * blocks) - audNull.size()); }
        thisTime = audLastMs;
        thisIdx = audIdx;
        bufferLivePacket(thisTime, 0, audIdx, audNull, audBytes * blocks, 0, true);
        audLastMs += audMs * blocks;
      }
  };

} // namespace Mist

int main(int argc, char *argv[]) {
  Comms::defaultCommFlags = COMM_STATUS_NOKILL;
  DTSC::trackValidMask = TRACK_VALID_INT_PROCESS;
  Util::Config config(argv[0]);
  Util::Config::binaryType = Util::PROCESS;

  // Commandline options
  config.addOption("configuration", R"({
    "arg":"string",
    "arg_num":1,
    "help":"JSON configuration, or - (default) to read from stdin",
    "default":"-"
  })");
  config.addOption("json", R"({
    "long":"json",
    "short":"j",
    "help":"Output connector info in JSON format, then exit.",
    "value":[0]
  })");
  config.addOption("stream", R"({
    "arg":"string",
    "short":"s",
    "long":"stream",
    "help":"Stream name to output to"
  })");
  config.addOption("sources", R"-({
    "arg":"string",
    "short":"S",
    "long":"sources",
    "help":"Stream name of input(s) (may be given more than once)"
  })-");
  config.addOption("copyaudio", R"-({
    "arg":"string",
    "short":"a",
    "long":"copyaudio",
    "help":"Source to mux in audio from (streamname)"
  })-");
  config.addOption("sourcesync", R"-({
    "short":"Y",
    "long":"sourcesync",
    "help":"Attempt to synchronize the sources based on UTC timestamps"
  })-");
  config.addOption("aspect", R"-({
    "arg":"string",
    "short":"A",
    "long":"aspect",
    "help":"Aspect ratio mode (letterbox/crop/stretch)",
    "default":"letterbox"
  })-");
  config.addOption("scaling", R"-({
    "arg":"string",
    "short":"C",
    "long":"scaling",
    "help":"Scaling mode (integer/nearest/bilinear)",
    "default":"integer"
  })-");
  config.addOption("resolution", R"-({
    "arg":"string",
    "short":"r",
    "long":"resolution",
    "help":"Output resolution i.e. 1920x1080",
    "default":"1920x1080"
  })-");
  config.addOption("printtiming", R"-({
    "arg":"string",
    "short":"T",
    "long":"printtiming",
    "help":"Print timing information on sources",
    "default": "no"
  })-");
  config.addOption("target_fps", R"-({
    "arg":"integer",
    "short":"f",
    "long":"target_fps",
    "help":"Target frame rate or 0 for automatic",
    "default":"0"
  })-");

  if (!(config.parseArgs(argc, argv))) { return 1; }

  if (config.getBool("json")) {
    JSON::Value capa;
    capa["name"] = "Composer";
    capa["hrn"] = "Raw video and audio composer";
    capa["desc"] = "A raw media composer that lets you create picture-in-picture feeds, multiviewers, add image "
                   "overlays or backgrounds, have fallbacks when sources go down, and much more. It only acts on raw "
                   "media (UYVY video, PCM audio) so its sources must be decoded before it can use them, and the "
                   "result will usually need to be encoded before it can be send over a network connection.";
    capa["type"] = "standalone";
    capa["source"] = "1+";
    capa["source_match"].append("multiview");
    capa["source_match"].append("compose");
    capa["always_match"] = capa["source_match"];
    capa["codecs"][0u][0u].append("UYVY");
    addGenericProcessOptions(capa);

    capa["required"]["sink"]["name"] = "Target stream";
    capa["required"]["sink"]["help"] =
      "What stream the encoded track should be added to. Defaults to source stream. May contain variables.";
    capa["required"]["sink"]["type"] = "string";
    capa["required"]["sink"]["validate"][0u] = "streamname_with_wildcard_and_variables";

    capa["optional"]["resolution"]["name"] = "resolution";
    capa["optional"]["resolution"]["option"] = "--resolution";
    capa["optional"]["resolution"]["help"] = "Resolution of the output stream, e.g. 1920x1080";
    capa["optional"]["resolution"]["type"] = "str";
    capa["optional"]["resolution"]["default"] = "1920x1080";
    capa["optional"]["resolution"]["sort"] = "aca";

    capa["optional"]["copyaudio"]["name"] = "Copy audio";
    capa["optional"]["copyaudio"]["option"] = "--copyaudio";
    capa["optional"]["copyaudio"]["help"] =
      "Copy audio from the given stream name. You can change this mid-stream, but all sources must have the same "
      "sampling rate and sample size. The stream need not be used for visuals as well, but if it is, it will be kept "
      "in sync.";
    capa["optional"]["copyaudio"]["type"] = "str";
    capa["optional"]["copyaudio"]["validate"][0u] = "streamname_with_wildcard";

    /*
    capa["optional"]["audiochannels"]["name"] = "Audio channels";
    capa["optional"]["audiochannels"]["help"] = "How many channels the audio out should have. Sources will drop any excess
    channels, and copy existing channels to create missing channels. The channel count cannot change after track creation.";
    capa["optional"]["audiochannels"]["type"] = "int";
    capa["optional"]["audiochannels"]["default"] = "Match first audio source";
    */

    capa["optional"]["printtiming"]["name"] = "Print timing";
    capa["optional"]["printtiming"]["option"] = "--printtiming";
    capa["optional"]["printtiming"]["help"] = "Print timing information";
    capa["optional"]["printtiming"]["type"] = "select";
    capa["optional"]["printtiming"]["select"][0u][0u] = "no";
    capa["optional"]["printtiming"]["select"][0u][1u] = "Don't print";
    capa["optional"]["printtiming"]["select"][1u][0u] = "topleft";
    capa["optional"]["printtiming"]["select"][1u][1u] = "Top left corner of cell";
    capa["optional"]["printtiming"]["select"][2u][0u] = "topmid";
    capa["optional"]["printtiming"]["select"][2u][1u] = "Top middle of cell";
    capa["optional"]["printtiming"]["select"][3u][0u] = "topright";
    capa["optional"]["printtiming"]["select"][3u][1u] = "Top right of cell";
    capa["optional"]["printtiming"]["select"][4u][0u] = "middle";
    capa["optional"]["printtiming"]["select"][4u][1u] = "Middle of cell";
    capa["optional"]["printtiming"]["default"] = "no";
    capa["optional"]["printtiming"]["source_can_override"] = true;

    capa["optional"]["sourcesync"]["name"] = "Sync sources";
    capa["optional"]["sourcesync"]["option"] = "--sourcesync";
    capa["optional"]["sourcesync"]["help"] = "Attempt to sync sources by UTC timestamps";
    capa["optional"]["sourcesync"]["type"] = "bool";
    capa["optional"]["sourcesync"]["influences"].append("sourcesync_help");

    {
      JSON::Value & o = capa["optional"]["sourcesync_help"]; // sort after sourcesync
      o["type"] = "help";
      o["help"] = "You can use the timing tool available at <a href=\"https://mistserver.org/demos/timing.html\" "
                  "target=\"_blank\">mistserver.org/demos/timing.html</a> to sync your sources. Make sure you show the "
                  "timing QR code to each of your sources <b>using the same device</b>.";
      o["dependent"]["sourcesync"] = "true";
    }

    capa["optional"]["scaling"]["name"] = "Scaling algorithm";
    capa["optional"]["scaling"]["option"] = "--scaling";
    capa["optional"]["scaling"]["help"] = "Algorithm used for scaling input sources to fit grid cells";
    capa["optional"]["scaling"]["type"] = "select";
    capa["optional"]["scaling"]["select"][0u][0u] = "bilinear";
    capa["optional"]["scaling"]["select"][0u][1u] = "Bilinear (high quality, slower)";
    capa["optional"]["scaling"]["select"][1u][0u] = "nearest";
    capa["optional"]["scaling"]["select"][1u][1u] = "Nearest neighbor (fast, lower quality)";
    capa["optional"]["scaling"]["select"][2u][0u] = "integer";
    capa["optional"]["scaling"]["select"][2u][1u] = "Integer scaling (integer scales only, but fast+hq)";
    capa["optional"]["scaling"]["default"] = "integer";
    capa["optional"]["scaling"]["sort"] = "acb";
    capa["optional"]["scaling"]["source_can_override"] = true;

    capa["optional"]["aspect"]["name"] = "Aspect ratio handling";
    capa["optional"]["aspect"]["option"] = "--aspect";
    capa["optional"]["aspect"]["help"] = "How to handle aspect ratio mismatches between input and grid cells";
    capa["optional"]["aspect"]["type"] = "select";
    capa["optional"]["aspect"]["select"][0u][0u] = "letterbox";
    capa["optional"]["aspect"]["select"][0u][1u] = "Letterbox/Pillarbox (preserve aspect, add black bars)";
    capa["optional"]["aspect"]["select"][1u][0u] = "crop";
    capa["optional"]["aspect"]["select"][1u][1u] = "Crop to fill (preserve aspect, crop excess)";
    capa["optional"]["aspect"]["select"][2u][0u] = "stretch";
    capa["optional"]["aspect"]["select"][2u][1u] = "Stretch to fit (may distort image)";
    capa["optional"]["aspect"]["select"][3u][0u] = "pattern";
    capa["optional"]["aspect"]["select"][3u][1u] = "Repeat as pattern until cell is filled";
    capa["optional"]["aspect"]["default"] = "letterbox";
    capa["optional"]["aspect"]["sort"] = "acc";
    capa["optional"]["aspect"]["source_can_override"] = true;

    capa["optional"]["offlineaction"]["name"] = "Offline stream action";
    capa["optional"]["offlineaction"]["help"] =
      "What to do when a source goes offline or becomes otherwise unavailable";
    capa["optional"]["offlineaction"]["type"] = "select";
    capa["optional"]["offlineaction"]["select"][0u][0u] = "grey";
    capa["optional"]["offlineaction"]["select"][0u][1u] = "Turn pixels greyscale";
    capa["optional"]["offlineaction"]["select"][1u][0u] = "black";
    capa["optional"]["offlineaction"]["select"][1u][1u] = "Turn pixels black";
    capa["optional"]["offlineaction"]["select"][2u][0u] = "keep";
    capa["optional"]["offlineaction"]["select"][2u][1u] = "Keep as-is";
    capa["optional"]["offlineaction"]["select"][3u][0u] = "remove";
    capa["optional"]["offlineaction"]["select"][3u][1u] = "Remove immediately";
    capa["optional"]["offlineaction"]["default"] = "grey";
    capa["optional"]["offlineaction"]["sort"] = "acca";
    capa["optional"]["offlineaction"]["source_can_override"] = true;

    capa["optional"]["removetimeout"]["name"] = "Removal timeout";
    capa["optional"]["removetimeout"]["help"] =
      "When a source goes offline, how long to wait before removing it entirely (0 = never)";
    capa["optional"]["removetimeout"]["type"] = "int";
    capa["optional"]["removetimeout"]["unit"] = "ms";
    capa["optional"]["removetimeout"]["default"] = "15000";
    capa["optional"]["removetimeout"]["sort"] = "accb";
    capa["optional"]["removetimeout"]["source_can_override"] = true;

    capa["optional"]["target_fps"]["name"] = "Target framerate";
    capa["optional"]["target_fps"]["option"] = "--target_fps";
    capa["optional"]["target_fps"]["help"] =
      "Target output framerate in frames per second. If not set, will wait for any source to have a fresh frame before "
      "outputting, and output at least two frames per second even if nothing has changed.";
    capa["optional"]["target_fps"]["type"] = "int";
    capa["optional"]["target_fps"]["default"] = 0;
    capa["optional"]["target_fps"]["sort"] = "acd";

    std::cout << capa.toString() << std::endl;
    return -1;
  }

  Util::redirectLogsIfNeeded();

  if (config.getString("stream").size()) {
    inputMode = true;
    Mist::opt["sink"] = config.getString("stream");
    Mist::opt["source"] = config.getOption("sources", true);
    Mist::opt["aspect"] = config.getOption("aspect");
    Mist::opt["resolution"] = config.getOption("resolution");
    Mist::opt["scaling"] = config.getOption("scaling");
    Mist::opt["copyaudio"] = config.getOption("copyaudio");
    Mist::opt["sourcesync"] = config.getOption("sourcesync");
  } else {
    // read configuration
    if (config.getString("configuration") != "-") {
      Mist::opt = JSON::fromString(config.getString("configuration"));
    } else {
      std::string json, line;
      INFO_MSG("Reading configuration from standard input");
      while (std::getline(std::cin, line)) { json.append(line); }
      Mist::opt = JSON::fromString(json.c_str());
    }
  }

  if (Mist::opt.isMember("copyaudio") && (Mist::opt["copyaudio"].asStringRef().size() || Mist::opt["copyaudio"].isInt())) {
    audioSource = Mist::opt["copyaudio"].asString();
  }

  if (Mist::opt.isMember("printtiming")) {
    if (Mist::opt["printtiming"].isString()) {
      doFrameTiming = parseTiming(Mist::opt["printtiming"].asStringRef());
    } else if (Mist::opt["printtiming"].asBool()) {
      doFrameTiming = TIMPOS_TL;
    }
  }
  if (Mist::opt.isMember("sourcesync")) { attemptSync = Mist::opt["sourcesync"].asBool(); }

  // Check generic configuration variables
  if (!Mist::opt.isMember("source") || !Mist::opt["source"] || !Mist::opt["source"].isArray()) {
    FAIL_MSG("invalid source(s) in config (must be array)!");
    return 1;
  }

  if (!Mist::opt.isMember("sink") || !Mist::opt["sink"] || !Mist::opt["sink"].isString()) {
    FAIL_MSG("No sink set");
    return 1;
  }

  Util::Config::is_active = true;

  // run sink
  Util::setStreamName(Mist::opt["sink"].asStringRef());
  Mist::ProcessSink in(&co);
  co.getOption("output", true).append("-");

  in.run();
  Util::Config::is_active = false;
  Mist::sources.clear();
  return 0;
}

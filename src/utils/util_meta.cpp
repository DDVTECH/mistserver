#include "lib/defines.h"
#include "mist/encode.h"
#include <iostream>
#include <fstream>
#include <list>
#include <mist/shared_memory.h>
#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/util.h>
#include <mist/stream.h>
#include <mist/urireader.h>
#include <dirent.h>
#include <sstream>
#include <sys/stat.h>
#include <queue>

// Holds information per stream we are analysing
struct metaTrackMapping {
    std::string streamName;
    std::string metaPageName;
    std::list<std::string> trackPageNames;
};

// Queues of Mist pages found in directory
std::list<metaTrackMapping> mstMetaQueue;
std::queue<std::string> mstMetaPages;
std::queue<std::string> mstMetaTracks;

// Flags & Internal variables
bool runDumpToDTSH;
bool isInteractive;
bool dumpDTSC;
bool writeToStdout;

std::string outputLocation;
std::string inputLocation;
std::string streamNameFilter;
// Size of queue at start of program
size_t queueSize;


///                           ### UTIL FUNCTIONS ###


/// Checks whether a given string starts with another string
/// \return true if \param str starts with \param pre
bool startsWith(const char *pre, const char *str){
  DONTEVEN_MSG("Checking whether %s starts with %s", str, pre);
  size_t lenpre = strlen(pre), lenstr = strlen(str);
  if (lenstr < lenpre){
    return false;
  }else{
    return memcmp(pre, str, lenpre) == 0;
  }
}

/// Checks whether a given fileName matches a requested stream name
/// \param fileName: the full name of the file we are matching
/// \param streamFilter: name of the stream we want to analyse
///   If it ends with a +, it will also match all wildcards
bool matchesStreamName(std::string fileName, std::string streamFilter){
  // Remove the MstData or MstTrak part
  fileName = fileName.substr(7);
  VERYHIGH_MSG("Matching fileName '%s' with filter '%s'", fileName.c_str(), streamFilter.c_str());
  // Add all ze wildcards if our filter ends with '+'
  if (streamFilter[streamFilter.size() - 1] == '+'){
    streamFilter = streamFilter.substr(0, streamFilter.size() - 1);
    VERYHIGH_MSG("Filter includes all wildcards. New filter is '%s'", streamFilter.c_str());
    uint64_t wildPosition = fileName.find('+');
    if (wildPosition != std::string::npos){
      fileName = fileName.substr(0, wildPosition);
      VERYHIGH_MSG("fileName contains a wildcard. New fileName is '%s'", fileName.c_str());
    }
  }
  uint64_t lenFileName = fileName.size(), lenFilter = streamFilter.size();
  // Match length of name
  if (lenFileName != lenFilter){
    return false;
  }else{
    return memcmp(fileName.c_str(), streamFilter.c_str(), lenFileName) == 0;
  }
}

/// Tries to cast a given string to a boolean based on the first character
/// \param defaultOption to return if the string is not cast-able
bool castStringToBool(std::string input, bool defaultOption = false){
  if (!input.size()){
    return defaultOption;
  }
  char firstChar = std::tolower(input[0]);
  if (firstChar == 'y'){
    return true;
  }else if (firstChar == 'n'){
    return false;
  }else{
    WARN_MSG("Cannot cast '%s' to bool. Returning %i.", input.c_str(), defaultOption);
  }
  return defaultOption;
}

/// Asks the user a question and catches a single line answer
/// \param question prompt to show to the user
/// \param defaultOption value to return if the response is empty
/// \param caseSensitive if false, cast everything to lowercase
std::string getInput(std::string question, std::string defaultOption = "", bool caseSensitive = false){
  std::string choice;

  std::cout << question;
  if (defaultOption != ""){
    std::cout << " (default = '" << defaultOption << "')";
  }
  std::cout << std::endl << "> ";
  std::getline(std::cin, choice);

  if (choice == ""){
    return defaultOption;
  }

  if (!caseSensitive){
    for (std::string::size_type i=0; i < choice.length(); ++i){
      choice[i] =  std::tolower(choice[i]);
    }
  }
  return choice;
}

/// Checks if the given file exists
/// \param fileName to file in CWD (or full/relative path to it)
/// \return true if file exists, false if it does not
bool fileExists(const std::string& fileName){
    struct stat buf;
    if (stat(fileName.c_str(), &buf) != -1){
        return true;
    }
    return false;
}

///                 ### DUMP DTSC, DTSH, RAX FUNCTIONS ####


/// Dumps packets found in MstData<streamname> files as string
/// \param filePath full path to MstData file
/// \return all packets in the file as a string
std::string dumpDTSCPackets(std::string filePath){
  char* tmpBuffer;
  size_t tmpBufIt = 0;
  size_t packetSize;
  size_t bytesRead;
  DTSC::Packet thisPacket;
  std::stringstream toReturn;

  // Open data file
  HTTP::URIReader fileReader(filePath);
  VERYHIGH_MSG("Dumping DTSC packets of file '%s", filePath.c_str());

  // Read all data available
  fileReader.readAll(tmpBuffer, bytesRead);
  VERYHIGH_MSG("Read %zu bytes", bytesRead);
  if (!bytesRead){
    ERROR_MSG("Input page is empty. Aborting dumping packets of current page...");
    return "";
  }

  // Dump packets while it is possible to extract a header
  while (tmpBufIt < bytesRead) {
    // Check if DTSC header exists at current location
    if (strncmp(tmpBuffer + tmpBufIt, "DT", 2) != 0){
      char magicHeader[5];
      strncpy(magicHeader, tmpBuffer + tmpBufIt, 4);
      magicHeader[4] = '\0';
      return toReturn.str();
    }
    
    // Get packet size (after the 4 bit magic byte header)
    packetSize = Bit::btohl(tmpBuffer + tmpBufIt + 4);
    VERYHIGH_MSG("Found DTSC packet of size %zu", packetSize);

    // Init packet data
    if (packetSize && bytesRead - tmpBufIt > packetSize + 8){
      thisPacket.reInit(tmpBuffer + tmpBufIt, packetSize + 8);
      if (!thisPacket) {
        ERROR_MSG("Invalid DTSC packet @ byte %zu-%zu", bytesRead-packetSize-8, bytesRead);
        return toReturn.str();
      }
      // Output details to output file
      toReturn << std::string(thisPacket.getData(), thisPacket.getDataLen());
    }
    tmpBufIt += packetSize + 8;
  }
  return toReturn.str();
}

/// Dumps dtsh file and dtsc packet info to output location
/// \param fileName name of the page in shared memory. Also used as derive the name of the output file
/// \param addComma add a comma at the end of stdio output to make it a valid JSON output
void dumpDTSH(std::string fileName, bool addComma){ 
  std::ofstream outputFile;
  // Meta pages are stored as: MstMetaSTREAMNAME, so remove first 7 chars to get the stream name
  std::string streamName = fileName.substr(7);
  // Quickly check if we have access to the file
  std::string metaPath = inputLocation + fileName;
  if (access(metaPath.c_str(), 0) != 0){
    ERROR_MSG("Skipping stream '%s' since we are not able to open '%s'", streamName.c_str(), metaPath.c_str());
    return;
  }
  HIGH_MSG("Opening stream '%s' MstMeta file at '%s'", streamName.c_str(), metaPath.c_str());
  DTSC::Meta M(streamName, metaPath, false);

  // Dump metadata to .JSON 
  if (Util::printDebugLevel <= DLVL_MEDIUM){
    JSON::Value metaAsJSON;
    if (Util::printDebugLevel <= DLVL_INFO){
      M.getHealthJSON(metaAsJSON);
    }else{
      M.toJSON(metaAsJSON);
    }
    std::stringstream ss(metaAsJSON);
    if (writeToStdout){
      printf("\"%s\":%s", streamName.c_str(), ss.str().c_str());
      if (addComma){
        printf(",\n");
      }
    }else{
      std::string outputPath = Encodings::URL::encode(outputLocation + streamName + ".json", "/:=@[]");
      VERYHIGH_MSG("Writing DTSC file to '%s'", outputPath.c_str());
      outputFile.open(outputPath.c_str());
      outputFile << ss.str() << std::endl;
      outputFile.close();
    }
  }

  // DTSH export
  if (runDumpToDTSH){
    // Create DTSH file
    M.toFile(Encodings::URL::encode(outputLocation + fileName + ".dtsh", "/:=@[]"));
  }

  // We are done if dumping packets is disabled
  if (!dumpDTSC){
    return;
  }
  // Else we want to index data pages in order to parse DTSC packets
  DIR *dir;
  struct dirent *ent;
  if ((dir = opendir (inputLocation.c_str())) != NULL) {
    while ((ent = readdir (dir)) != NULL) {
      // Find all pages of MstDataSTREAMNAME*
      if (startsWith(std::string("MstData" + streamName).c_str(), ent->d_name)){
        VERYHIGH_MSG("Found data file '%s'", ent->d_name);

        // Quickly check if we have access to the file
        std::string filePath = std::string(inputLocation) + ent->d_name;
        if (access(filePath.c_str(), 0) != 0){
          ERROR_MSG("Skipping file '%s' since we are not able to open it", filePath.c_str());
          continue;
        }

        std::string dtscOutputLocation = Encodings::URL::encode(outputLocation + ent->d_name + ".dtsc", "/:=@[]");
        // Output dtsc packets
        VERYHIGH_MSG("Writing DTSC packets to '%s", dtscOutputLocation.c_str());
        outputFile.open(dtscOutputLocation.c_str());
        outputFile << dumpDTSCPackets(filePath);
        outputFile.close();
      }else{
        DONTEVEN_MSG("Ignoring file %s", ent->d_name);
      }
    }
    closedir (dir);
  }else{
    FAIL_MSG("Could not open input location '%s'.", inputLocation.c_str());
    return;
  }
}

/// Dumps to output location as RAX
/// \param fileName of the page in shared memory
void dumpRax(std::string fileName){
  std::ofstream outputFile;
  std::string raxAsString;

  // Quickly check if we have access to the file
  std::string filePath = inputLocation + fileName;
  if (access(filePath.c_str(), 0) != 0){
    ERROR_MSG("Skipping file '%s' since we are not able to open it", filePath.c_str());
    return;
  }

  VERYHIGH_MSG("Analysing '%s'", filePath.c_str());
  IPC::sharedFile shmPage(fileName, 0, false, false, inputLocation);
  const Util::RelAccX raxObj(shmPage.mapped, false);
  if (raxObj.isReady()){
    raxAsString = raxObj.getRaxAsString(2, false);
    if(writeToStdout){
      printf("\n%s:\n%s\n", filePath.c_str(), raxAsString.c_str());    
    }else{
      std::string outputPath = Encodings::URL::encode(outputLocation + fileName + "_rax.txt", "/:=@[]");
      VERYHIGH_MSG("Dumping '%s' to '%s'", inputLocation.c_str(), outputPath.c_str());
      outputFile.open(outputPath.c_str());
      outputFile << raxAsString;
      outputFile.close();
      HIGH_MSG("Output written to '%s'", outputPath.c_str());
    }
  }else{
    FAIL_MSG("Unable to dump to RAX");
  }
}


///                   ### QUEUE CONSTRUCTION FUNCTIONS ####


/// Fills the queues of subprograms to run based on found MstMeta and MstTrak files
void constructQueue(){
  std::string currentFile;
  std::string shmPath;

  // First parse all Meta pages
  while (mstMetaPages.size()){
    currentFile = mstMetaPages.front();
    struct metaTrackMapping queueObj;
    queueObj.streamName = currentFile.substr(7);
    queueObj.metaPageName = currentFile;
    // queueObj.trackPageNames = [];
    mstMetaQueue.push_front(queueObj);
    mstMetaPages.pop();
  }

  // Then add all tracks to the right Meta queue
  while (mstMetaTracks.size()){
    currentFile = mstMetaTracks.front();
    std::string streamName = currentFile.substr(7, currentFile.rfind('@') - 7);
    VERYHIGH_MSG("Parsing trak '%s' corresponding with stream '%s'", currentFile.c_str(), streamName.c_str());
    for (std::list<metaTrackMapping>::iterator it = mstMetaQueue.begin(); it != mstMetaQueue.end(); ++it){
      if (it->streamName == streamName){
        it->trackPageNames.push_front(currentFile);
      }
    }
    mstMetaTracks.pop();
  }
}


/// Iterates over all page names in the queue and runs the appropriate subprogams
void parseQueue(){
  std::string currentFile;
  std::string shmPath;
  uint32_t commasToAdd = mstMetaQueue.size() - 1;
  bool outputToJSON = Util::printDebugLevel <= DLVL_MEDIUM;

  // Handle dtsc dumps
  // Add JSON { character if we are outputting to JSON only
  if (outputToJSON){
    printf("{");
  }
  for (std::list<metaTrackMapping>::iterator it = mstMetaQueue.begin(); it != mstMetaQueue.end(); ++it){
    INFO_MSG("Parsing stream '%s'", it->streamName.c_str());
    dumpDTSH(it->metaPageName, commasToAdd);
    commasToAdd--;

    // If the loglevel is low, we only want to print the Meta::toJSON
    if (!outputToJSON){
      dumpRax(it->metaPageName);
    }

    while (it->trackPageNames.size()){
      VERYHIGH_MSG("Parsing track '%s'", it->trackPageNames.front().c_str());
      // If the loglevel is low, we only want to print the Meta::toJSON
      if (!outputToJSON){
        dumpRax(it->trackPageNames.front());
      }
      it->trackPageNames.pop_front();
    }
  }
  if (outputToJSON){
    printf("}\n");
  }
}

/// Clears given (string) queue of all items
void clearQueue(std::queue<std::string> & queuedList){
  while (!queuedList.empty()){
    queuedList.pop();
  }
}

/// Checks whether to add a page to the queue or filter it
bool shouldAddToQueue(std::string pageName){
  // If there is no filter, add them all
  if (streamNameFilter == ""){
    return true;
  }

  // Compare if pageName starts with streamNameFilter
  // Since page names start with MstMeta or MstTrak, remove the first 7 chars from comparison
  if (matchesStreamName(pageName.c_str(), streamNameFilter.c_str())){
    return true;
  }
  return false;
}

///                             ### INIT FUNCTIONS ###

/// Indexes pages from the input location and filters them based on stream name, if given
/// \return False if the we are not able to read the input directory for any reason
bool indexPages(){
  clearQueue(mstMetaPages);
  clearQueue(mstMetaTracks);

  // List all pages and add them to the queue
  DIR *dir;
  struct dirent *ent;
  if ((dir = opendir (inputLocation.c_str())) != NULL) {
    while ((ent = readdir (dir)) != NULL) {
      // Find all pages in directory
      if (startsWith("MstMeta", ent->d_name)){
        // Filter based on stream name
        if (shouldAddToQueue(ent->d_name)){
          mstMetaPages.push(ent->d_name);
        }
      }else if (startsWith("MstTrak", ent->d_name)){
        // Filter based on stream name
        if (shouldAddToQueue(ent->d_name)){
          mstMetaTracks.push(ent->d_name);
        }
      }else{
        DONTEVEN_MSG("Ignoring file %s", ent->d_name);
      }
    }
    closedir (dir);
  }else{
    FAIL_MSG("Could not open input location '%s'.", inputLocation.c_str());
    return false;
  }
  return true;
}

/// Allows users to select subprograms and filters interactively
/// \return whether the operation succeeded
bool interactiveArgumentSelection(){
  // Re-init
  clearQueue(mstMetaPages);
  clearQueue(mstMetaTracks);
  std::string question;

  // Ask for input and output locations
  outputLocation = getInput("Where should we save the output? ('-' for stdout)", outputLocation, false);
  inputLocation = getInput("What directory contains the input?", inputLocation, false);

  // List all pages and add them to the queue
  DIR *dir;
  struct dirent *ent;
  if ((dir = opendir(inputLocation.c_str())) != NULL) {
    while ((ent = readdir(dir)) != NULL) {
      if (startsWith("MstMeta", ent->d_name)){
        question = std::string("Add track ") + ent->d_name + " to queue? (Y/n)";
        if (castStringToBool(getInput(question), true)){
          mstMetaPages.push(ent->d_name);
        }
      }else if (startsWith("MstTrak", ent->d_name)){
        question = std::string("Add stream ") + ent->d_name + " to queue? (Y/n)";
        if (castStringToBool(getInput(question), true)){
          mstMetaTracks.push(ent->d_name);
        }
      }
    }
    closedir(dir);
  }else{
    FAIL_MSG("Could not open input location '%s'.", inputLocation.c_str());
    return false;
  }

  // Ask for subprograms to run
  if (runDumpToDTSH){
    runDumpToDTSH = castStringToBool(getInput("Dump to DTSH? (Y/n)"), runDumpToDTSH);
  }else{
    runDumpToDTSH = castStringToBool(getInput("Dump to DTSH? (y/N)"), runDumpToDTSH);
  }
  if (dumpDTSC){
    dumpDTSC = castStringToBool(getInput("Dump DTSC packets? (Y/n)"), dumpDTSC);
  }else{
    dumpDTSC = castStringToBool(getInput("Dump DTSC packets? (y/N)"), dumpDTSC);
  }

  return true;
}

/// Parses all arguments, indexes available pages and then runs the main loop
int main(int argc, char **argv){
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  JSON::Value opt;

  // Only parse pages with a streamname starting with the given string
  opt["arg"] = "string";
  opt["default"] = "";
  opt["arg_num"] = 1;
  opt["help"] = "Name of the stream we want to parse pages from. Parses all streams by default";
  conf.addOption("streamname", opt);
  opt.null();

  // Path to where we want to save logs or dumps
  opt["arg"] = "string";
  opt["short"] = "o";
  opt["long"] = "out";
  opt["value"][0u] = "-";
  opt["help"] = "Path to output location. Writes to stdout by default";
  conf.addOption("output", opt);
  opt.null();
  // Path to location of memory pages
  opt["arg"] = "string";
  opt["short"] = "i";
  opt["long"] = "folder";
  opt["value"][0u] = "/dev/shm";
  opt["help"] = "Path to data pages. Reads from '/dev/shm' by default";
  conf.addOption("input", opt);
  opt.null();
  // Dump to DTSH
  opt["short"] = "d";
  opt["long"] = "dumpDTSH";
  opt["value"][0u] = 0;
  opt["help"] = "Add this flag to write to a DTSH file";
  conf.addOption("runDumpToDTSH", opt);
  opt.null();
  // Dump to DTSC
  opt["short"] = "p";
  opt["long"] = "dumpDTSC";
  opt["value"][0u] = 0;
  opt["help"] = "Add this flag to dump to a DTSC packets";
  conf.addOption("dumpDTSC", opt);
  opt.null();
  // Enable/Disable interactive page selection
  opt["short"] = "m";
  opt["long"] = "manual";
  opt["value"][0u] = 0;
  opt["help"] = "Add this flag to interactively select arguments, stream names and tracks";
  conf.addOption("interactive", opt);
  opt.null();

  // Parse (default) arguments and init flags
  conf.parseArgs(argc, argv);

  runDumpToDTSH = conf.getBool("runDumpToDTSH");
  outputLocation = conf.getString("output");
  inputLocation = conf.getString("input");
  streamNameFilter = conf.getString("streamname");
  isInteractive = conf.getBool("interactive");
  dumpDTSC = conf.getBool("dumpDTSC");

  // If interactive, overwrite arguments and select pages
  if (isInteractive){
    if (!interactiveArgumentSelection()){
      return 1;
    }
  }else if (!indexPages()){
    FAIL_MSG("Unable to index pages.");
    return 1;
  }

  if (outputLocation[0] == '-'){
    writeToStdout = true;
    // If there is no output location set, set it to the TMP directory
    if (runDumpToDTSH || dumpDTSC){
      if (conf.getString("output")[0] == '-'){
        outputLocation = Util::getTmpFolder();
      }else{
        outputLocation = conf.getString("output");
      }
      WARN_MSG("No output location is set. Defaulting to '%s'", outputLocation.c_str());
    }
  }else{
    writeToStdout = false;
  }

  // If the user forgot to add a / to the end of the input and output folders
  // add them to prevent errors
  if (*inputLocation.rbegin() != '/'){
    inputLocation = inputLocation + "/";
  }
  if (outputLocation[0] != '-' && *outputLocation.rbegin() != '/'){
    outputLocation = outputLocation + "/";
  }
  // Create output folder
  if (access(outputLocation.c_str(), 0) != 0){
    mkdir(outputLocation.c_str(),
          S_IRWXU | S_IRWXG | S_IRWXO);
  }

  // Determine what subprograms to run and copy files if the input is not live
  constructQueue();
  // Finally run main loop and exit with appropriate exit code
  parseQueue();
  return 0;
}

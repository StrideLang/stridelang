#include "stridec/board_builder.hpp"

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/platformnode.h"
#include "stride/stridegen/featureresolver.hpp"
#include "stride/stridegen/hardwareresolver.hpp"
#include "stride/stridegen/stridesystem.hpp"
#include "stride/stridegen/toolmanager.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/cachemanager.h"

#ifdef STRIDEC_EMBEDDED_RUNTIME
#include "stridec/embedded_runtime.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace strd {

namespace {

std::string toLower(std::string s) {
  for (char &c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string toUpper(std::string s) {
  for (char &c : s) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

bool parseTrailingDigits(const std::string &s, size_t prefixLen, int &outNum) {
  if (s.size() <= prefixLen) {
    return false;
  }
  for (size_t i = prefixLen; i < s.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
      return false;
    }
  }
  outNum = std::stoi(s.substr(prefixLen));
  return true;
}

int runSubprocess(const std::string &exe, const std::vector<std::string> &args,
                  bool verbose, std::ostream &out) {
  if (verbose) {
    out << "  -> " << exe;
    for (const auto &a : args) {
      out << " " << a;
    }
    out << "\n";
  }

#ifdef _WIN32
  auto quoteWin = [](const std::string &arg) -> std::string {
    if (arg.empty()) {
      return "\"\"";
    }
    if (arg.find_first_of(" \t\"") == std::string::npos) {
      return arg;
    }
    std::string res = "\"";
    for (size_t i = 0; i < arg.size(); ++i) {
      size_t backslashes = 0;
      while (i < arg.size() && arg[i] == '\\') {
        ++backslashes;
        ++i;
      }
      if (i == arg.size()) {
        res.append(backslashes * 2, '\\');
        break;
      } else if (arg[i] == '"') {
        res.append(backslashes * 2 + 1, '\\');
        res.push_back('"');
      } else {
        res.append(backslashes, '\\');
        res.push_back(arg[i]);
      }
    }
    res.push_back('"');
    return res;
  };

  std::string cmdLine = quoteWin(exe);
  for (const auto &a : args) {
    cmdLine += " " + quoteWin(a);
  }

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
  mutableCmd.push_back('\0');

  if (!CreateProcessA(exe.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE,
                      0, nullptr, nullptr, &si, &pi)) {
    return -1;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exitCode = 1;
  GetExitCodeProcess(pi.hProcess, &exitCode);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return static_cast<int>(exitCode);
#else
  std::string cmd = "\"" + exe + "\"";
  for (const auto &a : args) {
    cmd += " \"" + a + "\"";
  }
  return std::system(cmd.c_str());
#endif
}

std::string findExecutableInDir(const std::filesystem::path &dir,
                                const std::string &toolName) {
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) ||
      !std::filesystem::is_directory(dir, ec)) {
    return {};
  }
#ifdef _WIN32
  std::filesystem::path candidate = dir / (toolName + ".exe");
  if (std::filesystem::is_regular_file(candidate, ec)) {
    return candidate.string();
  }
#endif
  std::filesystem::path plain = dir / toolName;
  if (std::filesystem::is_regular_file(plain, ec)) {
    return plain.string();
  }
  return {};
}

std::string findInPath(const std::string &toolName) {
  const char *pathEnv = std::getenv("PATH");
  if (!pathEnv) {
    return {};
  }
#ifdef _WIN32
  const char sep = ';';
#else
  const char sep = ':';
#endif
  std::stringstream ss(pathEnv);
  std::string entry;
  while (std::getline(ss, entry, sep)) {
    if (entry.empty()) {
      continue;
    }
    auto found = findExecutableInDir(std::filesystem::path(entry), toolName);
    if (!found.empty()) {
      return found;
    }
  }
  return {};
}

std::string findInArduinoPackages(const std::string &packageSubdir,
                                  const std::string &toolName) {
  std::vector<std::filesystem::path> roots;
#ifdef _WIN32
  if (const char *localApp = std::getenv("LOCALAPPDATA")) {
    roots.push_back(std::filesystem::path(localApp) / "Arduino15" / "packages" /
                    "arduino" / "tools" / packageSubdir);
  }
#elif defined(__APPLE__)
  if (const char *home = std::getenv("HOME")) {
    roots.push_back(std::filesystem::path(home) / "Library" / "Arduino15" /
                    "packages" / "arduino" / "tools" / packageSubdir);
  }
#else
  if (const char *home = std::getenv("HOME")) {
    roots.push_back(std::filesystem::path(home) / ".arduino15" / "packages" /
                    "arduino" / "tools" / packageSubdir);
  }
#endif

  std::error_code ec;
  std::vector<std::filesystem::path> candidates;
  for (const auto &root : roots) {
    if (!std::filesystem::exists(root, ec) ||
        !std::filesystem::is_directory(root, ec)) {
      continue;
    }
    for (const auto &verDir : std::filesystem::directory_iterator(root, ec)) {
      if (!verDir.is_directory(ec)) {
        continue;
      }
      auto binDir = verDir.path() / "bin";
      auto exe = findExecutableInDir(binDir, toolName);
      if (!exe.empty()) {
        candidates.push_back(exe);
      }
    }
  }
  if (!candidates.empty()) {
    std::sort(candidates.begin(), candidates.end());
    return candidates.back().string();
  }
  return {};
}

} // namespace

bool inspectProgramBoard(const std::string &inputFile,
                         const std::string &strideRoot,
                         const std::string &boardOverride,
                         BoardSpec &outSpec, std::string &errorMsg) {
  ASTNode tree = AST::parseFile(inputFile.c_str());
  if (!tree) {
    errorMsg = "failed to parse '" + inputFile + "'";
    return false;
  }

  auto systems = ASTQuery::getSystemNodes(tree);
  if (systems.empty()) {
    if (!boardOverride.empty()) {
      outSpec.platformName = "AVR";
      outSpec.boardName = boardOverride;
      outSpec.frameworkName = "AVR";
      outSpec.frameworkVersion = "1.0";
      return true;
    }
    return false;
  }

  const auto &sysNode = systems.front();
  std::string systemName = sysNode->platformName();
  std::string targetBoard = boardOverride;
  if (targetBoard.empty()) {
    auto hw = sysNode->hwPlatforms();
    if (!hw.empty()) {
      targetBoard = hw.front();
    }
  }
  if (targetBoard.empty()) {
    return false;
  }

  std::string effectiveRoot =
      strideRoot.empty() ? ASTFunctions::getDefaultStrideRoot() : strideRoot;

  outSpec.platformName = systemName;
  outSpec.boardName = targetBoard;
  outSpec.frameworkName = systemName;
  outSpec.frameworkVersion =
      (sysNode->majorVersion() >= 0 && sysNode->minorVersion() >= 0)
          ? (std::to_string(sysNode->majorVersion()) + "." +
             std::to_string(sysNode->minorVersion()))
          : "1.0";

  StrideSystem sys(effectiveRoot, systemName, sysNode->majorVersion(),
                   sysNode->minorVersion(), {}, FeatureConfig());
  auto frameworks = sys.getFrameworks();
  if (!frameworks.empty()) {
    outSpec.frameworkName = frameworks.front()->getFramework();
    outSpec.frameworkVersion = frameworks.front()->getFrameworkVersion();
  }
  return true;
}

std::string generateBoardHeader(const ResolvedHardware &hw,
                                std::string &errorMsg) {
  auto pinsNode = hw.property("pins");
  if (!pinsNode || pinsNode->getNodeType() != AST::List) {
    errorMsg = "hardware '" + hw.name() + "' has no pins list";
    return {};
  }

  struct PinEntry {
    int output = 0;
    int direction = 0;
    int input = 0;
    int bit = 0;
    bool valid = false;
  };

  struct RawPin {
    std::string name;
    std::string port;
    int bit = 0;
    std::optional<int> explicitNumber;
  };

  std::vector<RawPin> rawPins;
  int maxExplicitOrDigital = -1;

  for (const auto &item : pinsNode->getChildren()) {
    if (item->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(item);
    RawPin pin;
    pin.name = decl->getName();
    pin.port = hw.entryString("pins", pin.name, "port");
    auto bitOpt = hw.entryInt("pins", pin.name, "bit");
    if (pin.port.empty() || !bitOpt) {
      continue;
    }
    pin.bit = static_cast<int>(*bitOpt);
    if (auto numOpt = hw.entryInt("pins", pin.name, "number")) {
      pin.explicitNumber = static_cast<int>(*numOpt);
      maxExplicitOrDigital = std::max(maxExplicitOrDigital, *pin.explicitNumber);
    } else if (pin.name.size() >= 2 && pin.name[0] == 'D') {
      int dNum = 0;
      if (parseTrailingDigits(pin.name, 1, dNum)) {
        pin.explicitNumber = dNum;
        maxExplicitOrDigital = std::max(maxExplicitOrDigital, dNum);
      }
    }
    rawPins.push_back(std::move(pin));
  }

  const int analogBase =
      (maxExplicitOrDigital >= 0) ? (maxExplicitOrDigital + 1) : 14;

  std::map<int, PinEntry> indexedPins;
  int maxIndex = -1;
  for (const auto &pin : rawPins) {
    int idx = -1;
    if (pin.explicitNumber) {
      idx = *pin.explicitNumber;
    } else if (pin.name.size() >= 2 && pin.name[0] == 'A') {
      int aNum = 0;
      if (parseTrailingDigits(pin.name, 1, aNum)) {
        idx = analogBase + aNum;
      }
    }
    if (idx < 0) {
      // Alias pin such as `LED` that duplicates a numbered pin.
      continue;
    }

    auto outReg = hw.entryInt("ports", pin.port, "output");
    auto dirReg = hw.entryInt("ports", pin.port, "direction");
    auto inReg = hw.entryInt("ports", pin.port, "input");
    if (!outReg || !dirReg || !inReg) {
      errorMsg = "hardware '" + hw.name() + "' references gpioPort '" +
                 pin.port + "' without input/direction/output addresses";
      return {};
    }

    PinEntry entry;
    entry.output = static_cast<int>(*outReg);
    entry.direction = static_cast<int>(*dirReg);
    entry.input = static_cast<int>(*inReg);
    entry.bit = pin.bit;
    entry.valid = true;
    indexedPins[idx] = entry;
    maxIndex = std::max(maxIndex, idx);
  }

  if (maxIndex < 0) {
    errorMsg = "hardware '" + hw.name() + "' has no numbered digital/analog pins";
    return {};
  }

  const int pinCount = maxIndex + 1;
  const int64_t baud = hw.blockInt("upload", "baud").value_or(115200);

  std::ostringstream ss;
  ss << "/* Generated by Stride for hardware '" << hw.name() << "' */\n";
  ss << "#pragma once\n\n";
  ss << "#define STRIDE_PIN_COUNT " << pinCount << "\n";
  // Ports above the 8-bit data space (ATmega2560's H to L) need 16-bit
  // register tables in the runtime
  bool wideRegisters = false;
  for (const auto &pin : indexedPins) {
    if (pin.second.output > 0xFF || pin.second.direction > 0xFF ||
        pin.second.input > 0xFF) {
      wideRegisters = true;
    }
  }
  if (wideRegisters) {
    ss << "#define STRIDE_PIN_REG_WIDE 1\n";
  }

  auto emitArrayMacro = [&](const char *macroName, auto getter) {
    ss << "#define " << macroName << " { ";
    for (int i = 0; i < pinCount; ++i) {
      if (i > 0) {
        ss << ", ";
      }
      auto it = indexedPins.find(i);
      ss << (it != indexedPins.end() ? getter(it->second) : 0);
    }
    ss << " }\n";
  };

  emitArrayMacro("STRIDE_PIN_OUTPUT_REGS",
                 [](const PinEntry &e) { return e.output; });
  emitArrayMacro("STRIDE_PIN_DIRECTION_REGS",
                 [](const PinEntry &e) { return e.direction; });
  emitArrayMacro("STRIDE_PIN_INPUT_REGS",
                 [](const PinEntry &e) { return e.input; });
  emitArrayMacro("STRIDE_PIN_BITS", [](const PinEntry &e) { return e.bit; });
  ss << "#define STRIDE_SERIAL_BAUD " << baud << "UL\n";

  return ss.str();
}

std::string findAvrTool(const std::string &toolName,
                        const std::string &strideRoot) {
  // 1. PATH
  if (auto p = findInPath(toolName); !p.empty()) {
    return p;
  }

  // 2. ToolManager local configs
  if (!strideRoot.empty()) {
    ToolManager tm(strideRoot);
    std::string key = (toolName == "avrdude") ? "Avrdude" : "AvrGcc";
    auto it = tm.localTools.find(key);
    if (it != tm.localTools.end() && !it->second.empty()) {
      std::filesystem::path toolPath(it->second);
      auto sibling = findExecutableInDir(toolPath.parent_path(), toolName);
      if (!sibling.empty()) {
        return sibling;
      }
    }
  }

  // 3. Arduino15 package directories
  const std::string pkg = (toolName == "avrdude") ? "avrdude" : "avr-gcc";
  if (auto p = findInArduinoPackages(pkg, toolName); !p.empty()) {
    return p;
  }

#ifdef _WIN32
  for (const char *progDir :
       {"C:/Program Files (x86)/Arduino/hardware/tools/avr/bin",
        "C:/Program Files/Arduino/hardware/tools/avr/bin"}) {
    if (auto p = findExecutableInDir(progDir, toolName); !p.empty()) {
      return p;
    }
  }
#endif
  return {};
}

std::string detectBoardSerialPort(const ResolvedHardware &hw,
                                  std::string &errorMsg) {
  std::vector<std::string> usbIds = hw.nameListProperty("usb");
  if (usbIds.empty()) {
    errorMsg = "board '" + hw.name() + "' declares no USB VID:PID list";
    return {};
  }

#ifdef _WIN32
  // 1. Read active COM ports from HKLM\HARDWARE\DEVICEMAP\SERIALCOMM.
  std::set<std::string> activePorts;
  {
    HKEY hSerialComm = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                      KEY_READ, &hSerialComm) == ERROR_SUCCESS) {
      DWORD index = 0;
      char valueName[256];
      BYTE data[256];
      while (true) {
        DWORD nameLen = sizeof(valueName);
        DWORD dataLen = sizeof(data);
        DWORD type = 0;
        LONG rc = RegEnumValueA(hSerialComm, index++, valueName, &nameLen,
                                nullptr, &type, data, &dataLen);
        if (rc != ERROR_SUCCESS) {
          break;
        }
        if (type == REG_SZ && dataLen > 0) {
          activePorts.insert(
              toUpper(std::string(reinterpret_cast<const char *>(data))));
        }
      }
      RegCloseKey(hSerialComm);
    }
  }

  if (activePorts.empty()) {
    errorMsg = "no active serial (COM) ports found; connect the board or pass --port <COMx>";
    return {};
  }

  // 2. Match VID:PID pairs under HKLM\SYSTEM\CurrentControlSet\Enum\USB.
  std::vector<std::string> matches;
  HKEY hUsb = nullptr;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    "SYSTEM\\CurrentControlSet\\Enum\\USB", 0, KEY_READ,
                    &hUsb) == ERROR_SUCCESS) {
    DWORD devIdx = 0;
    char devKeyName[256];
    while (true) {
      DWORD devKeyLen = sizeof(devKeyName);
      if (RegEnumKeyExA(hUsb, devIdx++, devKeyName, &devKeyLen, nullptr, nullptr,
                        nullptr, nullptr) != ERROR_SUCCESS) {
        break;
      }
      std::string devUpper = toUpper(std::string(devKeyName));
      bool vidPidMatch = false;
      for (const auto &vidPid : usbIds) {
        auto colon = vidPid.find(':');
        if (colon == std::string::npos) {
          continue;
        }
        std::string needle = "VID_" + toUpper(vidPid.substr(0, colon)) +
                             "&PID_" + toUpper(vidPid.substr(colon + 1));
        if (devUpper.find(needle) != std::string::npos) {
          vidPidMatch = true;
          break;
        }
      }
      if (!vidPidMatch) {
        continue;
      }

      HKEY hDev = nullptr;
      if (RegOpenKeyExA(hUsb, devKeyName, 0, KEY_READ, &hDev) != ERROR_SUCCESS) {
        continue;
      }
      DWORD instIdx = 0;
      char instKeyName[256];
      while (true) {
        DWORD instKeyLen = sizeof(instKeyName);
        if (RegEnumKeyExA(hDev, instIdx++, instKeyName, &instKeyLen, nullptr,
                          nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
          break;
        }
        HKEY hParams = nullptr;
        std::string paramSubkey = std::string(instKeyName) + "\\Device Parameters";
        if (RegOpenKeyExA(hDev, paramSubkey.c_str(), 0, KEY_READ, &hParams) ==
            ERROR_SUCCESS) {
          char portBuf[128];
          DWORD portLen = sizeof(portBuf);
          DWORD type = 0;
          if (RegQueryValueExA(hParams, "PortName", nullptr, &type,
                               reinterpret_cast<LPBYTE>(portBuf),
                               &portLen) == ERROR_SUCCESS &&
              type == REG_SZ) {
            std::string portName(portBuf);
            if (activePorts.count(toUpper(portName))) {
              matches.push_back(portName);
            }
          }
          RegCloseKey(hParams);
        }
      }
      RegCloseKey(hDev);
    }
    RegCloseKey(hUsb);
  }

  if (matches.size() == 1) {
    return matches.front();
  }
  if (matches.size() > 1) {
    std::ostringstream msg;
    msg << "multiple matching serial ports found for '" << hw.name() << "': ";
    for (size_t i = 0; i < matches.size(); ++i) {
      if (i > 0) msg << ", ";
      msg << matches[i];
    }
    msg << "; specify one with --port";
    errorMsg = msg.str();
    return {};
  }
  errorMsg = "no attached USB serial port matched '" + hw.name() +
             "'; connect the board or specify --port <COMx>";
  return {};
#else
  std::vector<std::string> candidates;
  std::error_code ec;
  if (std::filesystem::exists("/dev", ec)) {
    for (const auto &entry : std::filesystem::directory_iterator("/dev", ec)) {
      std::string name = entry.path().filename().string();
      if (name.rfind("ttyACM", 0) == 0 || name.rfind("ttyUSB", 0) == 0 ||
          name.rfind("cu.usbmodem", 0) == 0 ||
          name.rfind("cu.usbserial", 0) == 0) {
        candidates.push_back(entry.path().string());
      }
    }
  }
  if (candidates.size() == 1) {
    return candidates.front();
  }
  if (candidates.empty()) {
    errorMsg = "no USB serial port found in /dev; connect the board or pass --port";
  } else {
    errorMsg = "multiple USB serial ports found in /dev; pass --port";
  }
  return {};
#endif
}

bool buildBoardFirmware(const BoardBuildOptions &opts, std::ostream &out,
                        std::ostream &err) {
  if (opts.inputFile.empty() || !std::filesystem::exists(opts.inputFile)) {
    err << "stride: error: input file not found: '" << opts.inputFile << "'\n";
    return false;
  }

  std::string strideRoot = opts.strideRoot.empty()
                               ? ASTFunctions::getDefaultStrideRoot()
                               : opts.strideRoot;

  BoardSpec spec;
  std::string inspectErr;
  if (!inspectProgramBoard(opts.inputFile, strideRoot, opts.boardOverride, spec,
                           inspectErr)) {
    err << "stride: error: "
        << (inspectErr.empty()
                ? "could not determine target board from 'use <System> on <Board>' (or --boardOverride)"
                : inspectErr)
        << "\n";
    return false;
  }

  HardwareResolver resolver(strideRoot);
  // A project's own board (a hardware file next to the program) comes first
  {
    std::error_code pathErr;
    auto programDir = std::filesystem::absolute(opts.inputFile, pathErr).parent_path();
    if (!pathErr) {
      resolver.addSearchDirectory(programDir.generic_string());
    }
  }
  std::string hwErr;
  auto hw = resolver.resolve(spec.platformName, spec.boardName, &hwErr);
  if (!hw) {
    err << "stride: error: " << hwErr << "\n";
    return false;
  }

  const std::string triple = hw->stringProperty("triple");
  const std::string cpu = hw->stringProperty("cpu");
  const std::string ccFlags = hw->stringProperty("compilerFlags");
  const int64_t clockHz = hw->intProperty("clock").value_or(0);
  if (triple.empty() || cpu.empty() || clockHz <= 0) {
    err << "stride: error: hardware '" << hw->name()
        << "' is missing triple, cpu, or clock\n";
    return false;
  }

  if (triple != "avr") {
    err << "stride: error: board firmware build currently supports triple 'avr' (got '"
        << triple << "')\n";
    return false;
  }

  std::filesystem::path inPath(opts.inputFile);
  std::string stem = inPath.stem().string();
  std::filesystem::path outPath =
      opts.outputFile.empty()
          ? (inPath.parent_path().empty() ? std::filesystem::path(stem + ".hex")
                                          : (inPath.parent_path() / (stem + ".hex")))
          : std::filesystem::path(opts.outputFile);

  std::filesystem::path hexPath = outPath;
  std::filesystem::path elfPath = outPath;
  if (outPath.extension() == ".elf") {
    hexPath.replace_extension(".hex");
  } else {
    elfPath.replace_extension(".elf");
  }

  std::filesystem::path workDir =
      opts.buildDir.empty()
          ? (outPath.parent_path().empty()
                 ? std::filesystem::path(stem + "_build")
                 : (outPath.parent_path() / (stem + "_build")))
          : std::filesystem::path(opts.buildDir);

  std::error_code ec;
  std::filesystem::create_directories(workDir, ec);
  if (outPath.has_parent_path()) {
    std::filesystem::create_directories(outPath.parent_path(), ec);
  }

  // 1. Locate AVR toolchain tools.
  const std::string avrGcc = findAvrTool("avr-gcc", strideRoot);
  const std::string avrObjcopy = findAvrTool("avr-objcopy", strideRoot);
  const std::string avrSize = findAvrTool("avr-size", strideRoot);
  if (avrGcc.empty() || avrObjcopy.empty()) {
    err << "stride: error: avr-gcc / avr-objcopy not found in PATH or Arduino15 tools\n";
    return false;
  }

  // 2. Compile Stride source to AVR object file.
  std::filesystem::path progObj = workDir / (stem + ".o");
  if (opts.verbose) {
    out << "Compiling " << opts.inputFile << " for " << hw->name() << " ("
        << triple << ", " << cpu << " @ " << clockHz << " Hz)\n";
  }

  {
    StrideEnvironment env(strideRoot);
    for (const auto &inc : opts.includeDirs) {
      env.addIncludePath(inc);
    }
    if (opts.noDefaultFeatures) {
      env.setUseDefaultFeatures(false);
    }
    for (const auto &f : opts.disabledFeatures) {
      env.disableFeature(f);
    }
    for (const auto &f : opts.featureToggles) {
      if (auto ferr = env.enableFeature(f)) {
        err << "stride: error: " << llvm::toString(std::move(ferr)) << "\n";
        return false;
      }
    }
    env.setOptimizationLevel(opts.optimizationLevel >= 0 ? opts.optimizationLevel : 2);
    env.setTarget(triple, cpu, "");
    env.setStrict(opts.strict);
    env.setCacheLine(static_cast<unsigned>(
        std::max<int64_t>(hw->intProperty("cacheLine").value_or(0), 0)));
    if (!opts.boardOverride.empty()) {
      env.setBoardOverride(opts.boardOverride);
    }

    if (!env.generateIr(opts.inputFile, false)) {
      err << "stride: error: Stride IR compilation failed\n";
      return false;
    }
    env.emitDomainStateBuffers();
    // Where each variable lives in the firmware's memory: domain state
    // blocks (__stride_<Domain>_state) and globals, by ELF symbol. A host
    // tool reads a memory or state dump with it. It goes in the program's
    // .strdcache, one per board, so the source folder stays clean.
    {
      const std::filesystem::path layoutPath =
          strd::CacheManager::instance().getCacheFilePath(
              opts.inputFile, hw->name(), ".layout.json");
      std::ofstream layout(layoutPath, std::ios::binary);
      if (layout) {
        layout << env.getProgramLayout().toJson();
        if (opts.verbose) {
          out << "Layout: " << layoutPath.string() << "\n";
        }
      } else {
        err << "stride: warning: could not write " << layoutPath.string()
            << "\n";
      }
    }
    if (!env.emitObjectFile(progObj.string(), triple, cpu, "", "static", "")) {
      err << "stride: error: failed to emit AVR object file\n";
      return false;
    }
  }

  // 3. Generate stride_board.h and compile C runtime.
  std::string boardErr;
  std::string boardHeader = generateBoardHeader(*hw, boardErr);
  if (boardHeader.empty()) {
    err << "stride: error: " << boardErr << "\n";
    return false;
  }
  std::filesystem::path boardHeaderPath = workDir / "stride_board.h";
  {
    std::ofstream bh(boardHeaderPath, std::ios::binary);
    if (!bh) {
      err << "stride: error: failed to write " << boardHeaderPath.string() << "\n";
      return false;
    }
    bh << boardHeader;
  }

  std::filesystem::path runtimeSrc =
      std::filesystem::path(strideRoot) / "frameworks" / spec.frameworkName /
      spec.frameworkVersion / "runtime" / "stride_avr_runtime.c";
  if (!std::filesystem::exists(runtimeSrc)) {
    err << "stride: error: AVR runtime not found: " << runtimeSrc.string() << "\n";
    return false;
  }
  std::filesystem::path runtimeObj = workDir / "stride_avr_runtime.o";

  std::string mmcuFlag = ccFlags.empty() ? ("-mmcu=" + cpu) : ccFlags;
  std::string fCpuFlag = "-DF_CPU=" + std::to_string(clockHz) + "UL";

  if (runSubprocess(avrGcc,
                    {mmcuFlag, fCpuFlag, "-Os", "-std=gnu11",
                     "-ffunction-sections", "-fdata-sections", "-include",
                     boardHeaderPath.string(), "-c", runtimeSrc.string(), "-o",
                     runtimeObj.string()},
                    opts.verbose, out) != 0) {
    err << "stride: error: avr-gcc failed compiling runtime\n";
    return false;
  }

  // The string runtime (Print, Concat, Format, ...), the same C file the
  // JIT uses, embedded in stridec so it always matches this compiler. The
  // linker keeps only the functions the program calls.
  std::vector<std::string> linkInputs = {progObj.string(), runtimeObj.string()};
#ifdef STRIDEC_EMBEDDED_RUNTIME
  {
    const std::filesystem::path rtDir = workDir / "stride_rt";
    const std::filesystem::path statusDir = rtDir / "stride" / "stridejit";
    std::error_code ec;
    std::filesystem::create_directories(statusDir, ec);
    auto writeEmbedded = [&](const std::filesystem::path &path,
                             const unsigned char *bytes, size_t size) {
      std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char *>(bytes),
                 static_cast<std::streamsize>(size));
      return static_cast<bool>(file);
    };
    const std::filesystem::path stringSrc = rtDir / "strideruntime_string.c";
    // The arrays end with a 0 that is not part of the file
    if (!writeEmbedded(stringSrc, kStringRuntimeSource,
                       sizeof(kStringRuntimeSource) - 1) ||
        !writeEmbedded(statusDir / "stride_status.h", kStatusHeaderSource,
                       sizeof(kStatusHeaderSource) - 1)) {
      err << "stride: error: failed to write the string runtime to "
          << rtDir.string() << "\n";
      return false;
    }
    std::vector<std::string> args = {mmcuFlag, "-Os", "-std=gnu11",
                                     "-ffunction-sections", "-fdata-sections",
                                     "-DSTRIDE_RT_NO_STDIO",
                                     "-I" + rtDir.string()};
    if (hw->blockString("types", "real") == "Float32") {
      args.push_back("-DSTRIDE_RT_REAL_IS_FLOAT");
    }
    const std::filesystem::path stringObj = workDir / "strideruntime_string.o";
    args.insert(args.end(),
                {"-c", stringSrc.string(), "-o", stringObj.string()});
    if (runSubprocess(avrGcc, args, opts.verbose, out) != 0) {
      err << "stride: error: avr-gcc failed compiling the string runtime\n";
      return false;
    }
    linkInputs.push_back(stringObj.string());
  }
#endif

  // 4. Link .elf and extract .hex.
  std::vector<std::string> linkArgs = {mmcuFlag, "-Os", "-Wl,--gc-sections"};
  linkArgs.insert(linkArgs.end(), linkInputs.begin(), linkInputs.end());
  linkArgs.insert(linkArgs.end(), {"-lm", "-o", elfPath.string()});
  if (runSubprocess(avrGcc, linkArgs, opts.verbose, out) != 0) {
    err << "stride: error: avr-gcc link failed\n";
    return false;
  }

  if (runSubprocess(avrObjcopy,
                    {"-O", "ihex", "-R", ".eeprom", elfPath.string(),
                     hexPath.string()},
                    opts.verbose, out) != 0) {
    err << "stride: error: avr-objcopy failed\n";
    return false;
  }

  out << "Built firmware: " << hexPath.string() << "\n";
  if (!avrSize.empty()) {
    runSubprocess(avrSize, {"--mcu=" + cpu, "-C", elfPath.string()},
                  opts.verbose, out);
  }

  // 5. Optional upload via avrdude.
  if (opts.upload) {
    const std::string avrdude = findAvrTool("avrdude", strideRoot);
    if (avrdude.empty()) {
      err << "stride: error: avrdude not found in PATH or Arduino15 tools\n";
      return false;
    }

    std::string port = opts.serialPort;
    if (port.empty()) {
      std::string portErr;
      port = detectBoardSerialPort(*hw, portErr);
      if (port.empty()) {
        err << "stride: error: " << portErr << "\n";
        return false;
      }
      out << "Detected " << hw->name() << " on " << port << "\n";
    }

    const std::string programmer =
        hw->blockString("upload", "programmer", "arduino");
    const int64_t baud = hw->blockInt("upload", "baud").value_or(115200);

    std::vector<std::string> uploadArgs;
    std::filesystem::path confPath =
        std::filesystem::path(avrdude).parent_path().parent_path() / "etc" /
        "avrdude.conf";
    if (std::filesystem::exists(confPath)) {
      uploadArgs.push_back("-C");
      uploadArgs.push_back(confPath.string());
    }
    uploadArgs.push_back("-p");
    uploadArgs.push_back(cpu);
    uploadArgs.push_back("-c");
    uploadArgs.push_back(programmer);
    uploadArgs.push_back("-b");
    uploadArgs.push_back(std::to_string(baud));
    uploadArgs.push_back("-P");
    uploadArgs.push_back(port);
    uploadArgs.push_back("-D");
    uploadArgs.push_back("-U");
    uploadArgs.push_back("flash:w:" + hexPath.string() + ":i");

    if (runSubprocess(avrdude, uploadArgs, opts.verbose, out) != 0) {
      err << "stride: error: avrdude upload failed\n";
      return false;
    }
    out << "Uploaded " << hexPath.string() << " to " << hw->name() << " on "
        << port << "\n";
  }

  return true;
}

void printBoardBuildHelp(std::ostream &os, bool upload) {
  os << "Usage: stride " << (upload ? "flash" : "build")
     << " [options] <file.stride>\n\n"
     << (upload ? "Build board firmware and upload it to a connected board.\n\n"
                : "Build board firmware (.hex and .elf). 'stride flash' also uploads it.\n\n")
     << "Options:\n"
     << "  -o <file.hex|file.elf>   Output firmware file (default: <input>.hex)\n"
     << "  --boardOverride=<name>   Override target board from 'use <System> on <Board>'\n";
  if (upload) {
    os << "  --port=<port>, -P <port> Serial port for upload (auto-detected if omitted)\n";
  }
  os << "  --build-dir=<dir>        Directory for intermediate artifacts\n"
     << "  -O0, -O1, -O2, -O3, -Os, -Oz\n"
     << "                           Optimization level (default: 2)\n"
     << "  --strict                 Make a String write that can be cut off an error\n"
     << "  -I <dir>                 Add include search path\n"
     << "  -D <name>, --feature=<name>, --features=<list>\n"
     << "                           Enable feature(s) or variant(s)\n"
     << "  --no-feature=<name>      Disable feature or variant\n"
     << "  --no-default-features    Disable declared default features\n"
     << "  --strideroot=<dir>       Custom STRIDEROOT directory\n"
     << "  -v, --verbose            Verbose build and upload output\n"
     << "  -h, --help               Show this help message\n";
}

int runBoardBuild(const std::vector<std::string> &args,
                  const std::string &strideRoot, bool upload, std::ostream &out,
                  std::ostream &err) {
  const std::string command = upload ? "flash" : "build";
  BoardBuildOptions opts;
  opts.strideRoot = strideRoot;
  opts.upload = upload;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string &arg = args[i];
    bool missingValue = false;
    auto nextVal = [&](const std::string &flag, std::string &val) -> bool {
      if (arg == flag) {
        if (i + 1 < args.size()) {
          val = args[++i];
        } else {
          missingValue = true;
        }
        return true;
      }
      if (arg.rfind(flag + "=", 0) == 0) {
        val = arg.substr(flag.size() + 1);
        return true;
      }
      return false;
    };
    std::string port;
    if (arg == "-h" || arg == "--help") {
      printBoardBuildHelp(out, upload);
      return 0;
    } else if (nextVal("-o", opts.outputFile)) {
    } else if (nextVal("--boardOverride", opts.boardOverride)) {
    } else if (nextVal("--port", port) || nextVal("-P", port)) {
      if (!upload) {
        err << "stride: error: '" << arg
            << "' only applies to uploading; use 'stride flash'\n";
        return 1;
      }
      opts.serialPort = port;
    } else if (nextVal("--build-dir", opts.buildDir)) {
    } else if (nextVal("--strideroot", opts.strideRoot)) {
    } else if (arg == "--upload" || arg == "--flash") {
      err << "stride: error: unknown option '" << arg
          << "'; use 'stride flash' to build and upload\n";
      return 1;
    } else if (arg == "--strict") {
      opts.strict = true;
    } else if (arg == "--no-default-features") {
      opts.noDefaultFeatures = true;
    } else if (arg == "-v" || arg == "--verbose") {
      opts.verbose = true;
    } else if (arg == "-O0") {
      opts.optimizationLevel = 0;
    } else if (arg == "-O1") {
      opts.optimizationLevel = 1;
    } else if (arg == "-O2" || arg == "-Os" || arg == "-Oz") {
      opts.optimizationLevel = 2;
    } else if (arg == "-O3") {
      opts.optimizationLevel = 3;
    } else if (arg.rfind("-I", 0) == 0) {
      if (arg.size() > 2) opts.includeDirs.push_back(arg.substr(2));
      else if (i + 1 < args.size()) opts.includeDirs.push_back(args[++i]);
      else missingValue = true;
    } else if (arg.rfind("-D", 0) == 0) {
      if (arg.size() > 2) opts.featureToggles.push_back(arg.substr(2));
      else if (i + 1 < args.size()) opts.featureToggles.push_back(args[++i]);
      else missingValue = true;
    } else if (std::string feat; nextVal("--feature", feat)) {
      opts.featureToggles.push_back(feat);
    } else if (std::string feats; nextVal("--features", feats)) {
      std::stringstream ss(feats);
      std::string item;
      while (std::getline(ss, item, ',')) {
        if (!item.empty()) opts.featureToggles.push_back(item);
      }
    } else if (std::string noFeat; nextVal("--no-feature", noFeat)) {
      opts.disabledFeatures.push_back(noFeat);
    } else if (!arg.empty() && arg[0] == '-') {
      err << "stride: error: unknown option '" << arg << "' for 'stride "
          << command << "'\n";
      return 1;
    } else if (!opts.inputFile.empty()) {
      err << "stride: error: 'stride " << command
          << "' takes one .stride file, got '" << opts.inputFile << "' and '"
          << arg << "'\n";
      return 1;
    } else {
      opts.inputFile = arg;
    }
    if (missingValue) {
      err << "stride: error: '" << arg << "' requires a value\n";
      return 1;
    }
  }
  if (opts.inputFile.empty()) {
    err << "stride: error: no input file specified\n";
    return 1;
  }
  return buildBoardFirmware(opts, out, err) ? 0 : 1;
}

} // namespace strd

#include "stridec/board_builder.hpp"

#include "stride/parser/ast.h"
#include "stride/stridegen/featureresolver.hpp"
#include "stride/stridegen/hardwareresolver.hpp"
#include "stride/stridegen/toolmanager.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/utils/astfunctions.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace strd {

namespace {

std::string toLowerCopy(std::string s) {
  for (char &c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string toUpperCopy(std::string s) {
  for (char &c : s) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return s;
}

std::string escapeJsonString(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
        out += buf;
      } else {
        out.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  return out;
}

std::string findExeInDir(const std::filesystem::path &dir,
                         const std::string &name) {
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) ||
      !std::filesystem::is_directory(dir, ec)) {
    return {};
  }
#ifdef _WIN32
  for (const std::string &cand : {name + ".exe", name + ".cmd", name}) {
#else
  for (const std::string &cand : {name}) {
#endif
    std::filesystem::path p = dir / cand;
    if (std::filesystem::exists(p, ec) &&
        std::filesystem::is_regular_file(p, ec)) {
      return p.string();
    }
  }
  return {};
}

std::string findExeInPath(const std::string &name) {
  const char *pathEnv = std::getenv("PATH");
  if (!pathEnv) {
    return {};
  }
  std::string pathStr(pathEnv);
#ifdef _WIN32
  const char sep = ';';
#else
  const char sep = ':';
#endif
  std::stringstream ss(pathStr);
  std::string dir;
  while (std::getline(ss, dir, sep)) {
    if (dir.empty()) {
      continue;
    }
    if (auto found = findExeInDir(dir, name); !found.empty()) {
      return found;
    }
  }
  return {};
}

std::string findRunnerTool(const std::string &exeName,
                           const std::string &toolManagerKey,
                           const std::string &strideRoot) {
  if (auto p = findExeInPath(exeName); !p.empty()) {
    return p;
  }
  if (!strideRoot.empty() && !toolManagerKey.empty()) {
    ToolManager tm(strideRoot);
    auto it = tm.localTools.find(toolManagerKey);
    if (it != tm.localTools.end() && !it->second.empty()) {
      std::filesystem::path p(it->second);
      if (std::filesystem::exists(p)) {
        return p.string();
      }
      if (auto sibling = findExeInDir(p.parent_path(), exeName);
          !sibling.empty()) {
        return sibling;
      }
    }
  }
  return {};
}

int runRunnerSubprocess(const std::string &exe,
                        const std::vector<std::string> &args, bool verbose,
                        std::ostream &out) {
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
  pid_t pid = fork();
  if (pid < 0) {
    return -1;
  }
  if (pid == 0) {
    std::vector<char *> cArgs;
    cArgs.reserve(args.size() + 2);
    cArgs.push_back(const_cast<char *>(exe.c_str()));
    for (const auto &a : args) {
      cArgs.push_back(const_cast<char *>(a.c_str()));
    }
    cArgs.push_back(nullptr);
    execvp(exe.c_str(), cArgs.data());
    _exit(127);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return -1;
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

std::string defaultDesktopDebugAdapter() {
#if defined(_WIN32)
  return "cppvsdbg";
#elif defined(__APPLE__)
  return "lldb";
#else
  return "cppdbg";
#endif
}

void writeTargetManifestFile(const std::filesystem::path &manifestPath,
                             const TargetDescriptor &desc, bool verbose,
                             std::ostream &out, std::ostream &err) {
  std::error_code ec;
  if (manifestPath.has_parent_path()) {
    std::filesystem::create_directories(manifestPath.parent_path(), ec);
  }
  std::ofstream f(manifestPath, std::ios::binary);
  if (!f) {
    err << "stride: warning: could not write " << manifestPath.string() << "\n";
    return;
  }
  f << targetDescriptorToJson(desc);
  if (verbose) {
    out << "Target manifest: " << manifestPath.string() << "\n";
  }
}

void printUnifiedHelp(std::ostream &os, const std::string &mode) {
  if (mode == "run") {
    os << "Usage: stride run [options] <file.stride>\n\n"
       << "Build and run a Stride program (executes desktop/JIT targets or "
          "flashes & monitors board targets).\n\n";
  } else if (mode == "flash") {
    printBoardBuildHelp(os, true);
    return;
  } else {
    os << "Usage: stride build [options] <file.stride>\n\n"
       << "Build a Stride program (board firmware .hex/.elf or desktop/JIT "
          "object/manifest).\n\n";
  }
  os << "Options:\n"
     << "  -o <file>                Output artifact path\n"
     << "  --boardOverride=<name>   Override target board from 'use <System> "
        "on <Board>'\n";
  if (mode == "run" || mode == "flash") {
    os << "  --port=<port>, -P <port> Serial port for board upload/monitor "
          "(auto-detected if omitted)\n"
       << "  --probe                  Flash using the board's debug probe "
          "(e.g. OpenOCD/SWD)\n"
       << "  --upload-method=<name>   Override board upload method "
          "(picotool, openocd, probe-rs, ...)\n"
       << "  --probe-interface=<cfg>  Override OpenOCD probe interface config "
          "(implies --probe)\n";
  }
  os << "  --build-dir=<dir>        Directory for intermediate artifacts\n"
     << "  -O0, -O1, -O2, -O3, -Os, -Oz\n"
     << "                           Optimization level (default: 2)\n"
     << "  --strict                 Make a String write that can be cut off an "
        "error\n"
     << "  -I <dir>                 Add include search path\n"
     << "  -D <name>, --feature=<name>, --features=<list>\n"
     << "                           Enable feature(s) or variant(s)\n"
     << "  --no-feature=<name>      Disable feature or variant\n"
     << "  --no-default-features    Disable declared default features\n"
     << "  --strideroot=<dir>       Custom STRIDEROOT directory\n"
     << "  -v, --verbose            Verbose build and run output\n"
     << "  -h, --help               Show this help message\n";
}

bool parseUnifiedOptions(const std::vector<std::string> &args,
                         const std::string &strideRoot,
                         const std::string &mode, BoardBuildOptions &opts,
                         bool &showHelp, std::ostream &err) {
  opts.strideRoot = strideRoot;
  opts.upload = (mode == "flash" || mode == "run");
  showHelp = false;

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
    std::string uploadMethodOverride;
    std::string probeInterfaceOverride;
    if (arg == "-h" || arg == "--help") {
      showHelp = true;
      return true;
    } else if (nextVal("-o", opts.outputFile)) {
    } else if (nextVal("--boardOverride", opts.boardOverride)) {
    } else if (nextVal("--port", port) || nextVal("-P", port)) {
      if (mode == "build") {
        err << "stride: error: '" << arg
            << "' only applies to uploading; use 'stride flash' or 'stride "
               "run'\n";
        return false;
      }
      opts.serialPort = port;
    } else if (arg == "--probe") {
      if (mode == "build") {
        err << "stride: error: '--probe' only applies to uploading; use 'stride "
               "flash' or 'stride run'\n";
        return false;
      }
      opts.useProbe = true;
    } else if (nextVal("--upload-method", uploadMethodOverride)) {
      if (mode == "build") {
        err << "stride: error: '" << arg
            << "' only applies to uploading; use 'stride flash' or 'stride "
               "run'\n";
        return false;
      }
      opts.uploadMethod = uploadMethodOverride;
    } else if (nextVal("--probe-interface", probeInterfaceOverride)) {
      if (mode == "build") {
        err << "stride: error: '" << arg
            << "' only applies to uploading; use 'stride flash' or 'stride "
               "run'\n";
        return false;
      }
      opts.probeInterface = probeInterfaceOverride;
      opts.useProbe = true;
    } else if (nextVal("--build-dir", opts.buildDir)) {
    } else if (nextVal("--strideroot", opts.strideRoot)) {
    } else if (arg == "--upload" || arg == "--flash") {
      err << "stride: error: unknown option '" << arg
          << "'; use 'stride flash' to build and upload\n";
      return false;
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
      if (arg.size() > 2)
        opts.includeDirs.push_back(arg.substr(2));
      else if (i + 1 < args.size())
        opts.includeDirs.push_back(args[++i]);
      else
        missingValue = true;
    } else if (arg.rfind("-D", 0) == 0) {
      if (arg.size() > 2)
        opts.featureToggles.push_back(arg.substr(2));
      else if (i + 1 < args.size())
        opts.featureToggles.push_back(args[++i]);
      else
        missingValue = true;
    } else if (std::string feat; nextVal("--feature", feat)) {
      opts.featureToggles.push_back(feat);
    } else if (std::string feats; nextVal("--features", feats)) {
      std::stringstream ss(feats);
      std::string item;
      while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
          opts.featureToggles.push_back(item);
        }
      }
    } else if (std::string noFeat; nextVal("--no-feature", noFeat)) {
      opts.disabledFeatures.push_back(noFeat);
    } else if (!arg.empty() && arg[0] == '-') {
      err << "stride: error: unknown option '" << arg << "' for 'stride "
          << mode << "'\n";
      return false;
    } else if (!opts.inputFile.empty()) {
      err << "stride: error: 'stride " << mode
          << "' takes one .stride file, got '" << opts.inputFile << "' and '"
          << arg << "'\n";
      return false;
    } else {
      opts.inputFile = arg;
    }

    if (missingValue) {
      err << "stride: error: '" << arg << "' requires a value\n";
      return false;
    }
  }
  return true;
}

} // namespace

std::vector<SerialDeviceInfo> enumerateSerialDevices() {
  std::vector<SerialDeviceInfo> devices;
#ifdef _WIN32
  std::map<std::string, std::string> activePorts;
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
          std::string port(reinterpret_cast<const char *>(data));
          activePorts[toUpperCopy(port)] = port;
        }
      }
      RegCloseKey(hSerialComm);
    }
  }

  if (activePorts.empty()) {
    return devices;
  }

  std::map<std::string, std::string> portToVidPid;
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
      std::string devUpper = toUpperCopy(std::string(devKeyName));
      std::string vidPid;
      auto vidPos = devUpper.find("VID_");
      auto pidPos = devUpper.find("PID_");
      if (vidPos != std::string::npos && pidPos != std::string::npos &&
          vidPos + 8 <= devUpper.size() && pidPos + 8 <= devUpper.size()) {
        vidPid = devUpper.substr(vidPos + 4, 4) + ":" +
                 devUpper.substr(pidPos + 4, 4);
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
        std::string paramSubkey =
            std::string(instKeyName) + "\\Device Parameters";
        if (RegOpenKeyExA(hDev, paramSubkey.c_str(), 0, KEY_READ, &hParams) ==
            ERROR_SUCCESS) {
          char portBuf[128];
          DWORD portLen = sizeof(portBuf);
          DWORD type = 0;
          if (RegQueryValueExA(hParams, "PortName", nullptr, &type,
                               reinterpret_cast<LPBYTE>(portBuf),
                               &portLen) == ERROR_SUCCESS &&
              type == REG_SZ) {
            std::string portUpper = toUpperCopy(std::string(portBuf));
            if (activePorts.count(portUpper) && !vidPid.empty()) {
              portToVidPid[portUpper] = vidPid;
            }
          }
          RegCloseKey(hParams);
        }
      }
      RegCloseKey(hDev);
    }
    RegCloseKey(hUsb);
  }

  for (const auto &[portUpper, portName] : activePorts) {
    SerialDeviceInfo info;
    info.port = portName;
    auto it = portToVidPid.find(portUpper);
    if (it != portToVidPid.end()) {
      info.vidPid = it->second;
    }
    devices.push_back(std::move(info));
  }
#else
  std::error_code ec;
  if (std::filesystem::exists("/dev", ec)) {
    for (const auto &entry : std::filesystem::directory_iterator("/dev", ec)) {
      std::string name = entry.path().filename().string();
      if (name.rfind("ttyACM", 0) == 0 || name.rfind("ttyUSB", 0) == 0 ||
          name.rfind("cu.usbmodem", 0) == 0 ||
          name.rfind("cu.usbserial", 0) == 0) {
        SerialDeviceInfo info;
        info.port = entry.path().string();
        devices.push_back(std::move(info));
      }
    }
  }
#endif
  return devices;
}

static std::shared_ptr<DeclarationNode>
uploadMethodBlock(const ResolvedHardware &hw, const std::string &methodBlock) {
  auto node = hw.blockValue("upload", methodBlock);
  if (node && node->getNodeType() == AST::Declaration) {
    return std::static_pointer_cast<DeclarationNode>(node);
  }
  return nullptr;
}

static std::string uploadMethodString(const ResolvedHardware &hw,
                                      const std::string &methodBlock,
                                      const std::string &key,
                                      const std::string &flatFallbackKey,
                                      const std::string &defaultValue = "") {
  if (auto sub = uploadMethodBlock(hw, methodBlock)) {
    if (auto val = sub->getPropertyValue(key)) {
      std::string text = HardwareResolver::valueText(val);
      if (!text.empty()) {
        return text;
      }
    }
  }
  std::string direct = hw.blockString("upload", key, "");
  if (!direct.empty()) {
    return direct;
  }
  if (!flatFallbackKey.empty()) {
    std::string alt = hw.blockString("upload", flatFallbackKey, "");
    if (!alt.empty()) {
      return alt;
    }
  }
  return defaultValue;
}

static std::optional<int64_t>
uploadMethodInt(const ResolvedHardware &hw, const std::string &methodBlock,
                const std::string &key, const std::string &flatFallbackKey) {
  auto parseInt = [](const ASTNode &val) -> std::optional<int64_t> {
    if (!val) {
      return std::nullopt;
    }
    if (val->getNodeType() == AST::Int) {
      return std::static_pointer_cast<ValueNode>(val)->getIntValue();
    }
    return std::nullopt;
  };
  if (auto sub = uploadMethodBlock(hw, methodBlock)) {
    if (auto val = parseInt(sub->getPropertyValue(key))) {
      return val;
    }
  }
  if (auto val = hw.blockInt("upload", key)) {
    return val;
  }
  if (!flatFallbackKey.empty()) {
    if (auto val = hw.blockInt("upload", flatFallbackKey)) {
      return val;
    }
  }
  return std::nullopt;
}

static std::string resolveUploadMethod(const ResolvedHardware &hw,
                                       const BoardBuildOptions &opts) {
  if (!opts.uploadMethod.empty()) {
    return toLowerCopy(opts.uploadMethod);
  }
  if (opts.useProbe) {
    std::string probeMethod = hw.blockString("upload", "probe", "");
    if (!probeMethod.empty()) {
      return toLowerCopy(probeMethod);
    }
    if (uploadMethodBlock(hw, "openocd") ||
        !hw.blockString("upload", "openocdInterface", "").empty() ||
        !hw.blockString("upload", "interface", "").empty()) {
      return "openocd";
    }
    if (uploadMethodBlock(hw, "probeRs")) {
      return "probe-rs";
    }
    return "openocd";
  }
  std::string def = hw.blockString("upload", "method", "");
  if (def.empty()) {
    def = hw.blockString("upload", "default", "avrdude");
  }
  return toLowerCopy(def);
}

bool uploadFirmwareWithRunner(const ResolvedHardware &hw,
                              const std::string &cpu,
                              const BoardBuildOptions &opts,
                              const std::filesystem::path &elfPath,
                              const std::filesystem::path &hexPath,
                              std::ostream &out, std::ostream &err) {
  const std::string strideRoot =
      opts.strideRoot.empty() ? ASTFunctions::getDefaultStrideRoot()
                              : opts.strideRoot;
  const std::string method = resolveUploadMethod(hw, opts);

  if (method == "none") {
    out << "Upload method is 'none'; skipping upload for " << hw.name() << "\n";
    return true;
  }

  auto resolvePort = [&](std::string &outPort) -> bool {
    outPort = opts.serialPort;
    if (outPort.empty()) {
      std::string portErr;
      outPort = detectBoardSerialPort(hw, portErr);
      if (outPort.empty()) {
        err << "stride: error: " << portErr << "\n";
        return false;
      }
      out << "Detected " << hw.name() << " on " << outPort << "\n";
    }
    return true;
  };

  if (method == "avrdude") {
    const std::string avrdude = findAvrTool("avrdude", strideRoot);
    if (avrdude.empty()) {
      err << "stride: error: avrdude not found in PATH or Arduino15 tools\n";
      return false;
    }
    std::string port;
    if (!resolvePort(port)) {
      return false;
    }
    const std::string programmer =
        uploadMethodString(hw, "avrdude", "programmer", "", "arduino");
    const int64_t baud =
        uploadMethodInt(hw, "avrdude", "baud", "").value_or(115200);

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

    if (runRunnerSubprocess(avrdude, uploadArgs, opts.verbose, out) != 0) {
      err << "stride: error: avrdude upload failed\n";
      return false;
    }
    out << "Uploaded " << hexPath.string() << " to " << hw.name() << " on "
        << port << "\n";
    return true;
  }

  if (method == "arduino-cli") {
    const std::string arduinoCli =
        findRunnerTool("arduino-cli", "ArduinoCli", strideRoot);
    if (arduinoCli.empty()) {
      err << "stride: error: arduino-cli not found in PATH or ToolManager\n";
      return false;
    }
    std::string port;
    if (!resolvePort(port)) {
      return false;
    }
    const std::string fqbn =
        uploadMethodString(hw, "arduinoCli", "fqbn", "", "");
    std::vector<std::string> uploadArgs = {"upload", "-p", port, "--input-file",
                                           hexPath.string()};
    if (!fqbn.empty()) {
      uploadArgs.push_back("--fqbn");
      uploadArgs.push_back(fqbn);
    }
    if (runRunnerSubprocess(arduinoCli, uploadArgs, opts.verbose, out) != 0) {
      err << "stride: error: arduino-cli upload failed\n";
      return false;
    }
    out << "Uploaded " << hexPath.string() << " to " << hw.name() << " on "
        << port << " via arduino-cli\n";
    return true;
  }

  if (method == "openocd") {
    const std::string openocd =
        findRunnerTool("openocd", "OpenOCD", strideRoot);
    if (openocd.empty()) {
      err << "stride: error: openocd not found in PATH or ToolManager\n";
      return false;
    }
    std::vector<std::string> uploadArgs;
    const std::string boardCfg =
        uploadMethodString(hw, "openocd", "boardCfg", "openocdBoard", "");
    const std::string iface =
        !opts.probeInterface.empty()
            ? opts.probeInterface
            : uploadMethodString(hw, "openocd", "interface",
                                 "openocdInterface", "");
    const std::string target =
        uploadMethodString(hw, "openocd", "target", "openocdTarget", "");
    const std::string transport =
        uploadMethodString(hw, "openocd", "transport", "openocdTransport", "");
    const std::optional<int64_t> speed =
        uploadMethodInt(hw, "openocd", "speed", "openocdSpeed");

    if (!boardCfg.empty() && opts.probeInterface.empty()) {
      uploadArgs.push_back("-f");
      uploadArgs.push_back(boardCfg);
    } else {
      if (!iface.empty()) {
        uploadArgs.push_back("-f");
        uploadArgs.push_back(iface);
      }
      if (!transport.empty()) {
        uploadArgs.push_back("-c");
        uploadArgs.push_back("transport select " + transport);
      }
      if (!target.empty()) {
        uploadArgs.push_back("-f");
        uploadArgs.push_back(target);
      }
    }
    if (speed.has_value()) {
      uploadArgs.push_back("-c");
      uploadArgs.push_back("adapter speed " + std::to_string(*speed));
    }
    uploadArgs.push_back("-c");
    uploadArgs.push_back("program " + elfPath.generic_string() +
                         " verify reset exit");
    if (runRunnerSubprocess(openocd, uploadArgs, opts.verbose, out) != 0) {
      err << "stride: error: openocd upload failed\n";
      return false;
    }
    out << "Uploaded " << elfPath.string() << " to " << hw.name()
        << " via openocd\n";
    return true;
  }

  if (method == "probe-rs") {
    const std::string probeRs =
        findRunnerTool("probe-rs", "ProbeRs", strideRoot);
    if (probeRs.empty()) {
      err << "stride: error: probe-rs not found in PATH\n";
      return false;
    }
    const std::string chip =
        uploadMethodString(hw, "probeRs", "chip", "", cpu);
    std::vector<std::string> uploadArgs = {"download", "--chip", chip,
                                           elfPath.string()};
    if (runRunnerSubprocess(probeRs, uploadArgs, opts.verbose, out) != 0) {
      err << "stride: error: probe-rs download failed\n";
      return false;
    }
    out << "Uploaded " << elfPath.string() << " to " << hw.name()
        << " via probe-rs\n";
    return true;
  }

  if (method == "picotool") {
    const std::string picotool =
        findRunnerTool("picotool", "Picotool", strideRoot);
    if (picotool.empty()) {
      err << "stride: error: picotool not found in PATH\n";
      return false;
    }
    std::vector<std::string> uploadArgs = {"load", "-x", elfPath.string()};
    if (runRunnerSubprocess(picotool, uploadArgs, opts.verbose, out) != 0) {
      err << "stride: error: picotool load failed\n";
      return false;
    }
    out << "Uploaded " << elfPath.string() << " to " << hw.name()
        << " via picotool\n";
    return true;
  }

  err << "stride: error: unsupported upload method '" << method
      << "' (supported: avrdude, arduino-cli, openocd, probe-rs, picotool, "
         "none)\n";
  return false;
}

bool inspectTargetDescriptor(const BoardBuildOptions &opts,
                             TargetDescriptor &outDesc, std::string &errorMsg) {
  if (opts.inputFile.empty() || !std::filesystem::exists(opts.inputFile)) {
    errorMsg = "input file not found: '" + opts.inputFile + "'";
    return false;
  }

  const std::string strideRoot =
      opts.strideRoot.empty() ? ASTFunctions::getDefaultStrideRoot()
                              : opts.strideRoot;
  std::filesystem::path inPath(opts.inputFile);
  std::string stem = inPath.stem().string();
  std::filesystem::path parentDir = inPath.parent_path();

  BoardSpec spec;
  std::string inspectErr;
  if (inspectProgramBoard(opts.inputFile, strideRoot, opts.boardOverride, spec,
                          inspectErr)) {
    HardwareResolver resolver(strideRoot);
    std::error_code pathErr;
    auto programDir =
        std::filesystem::absolute(opts.inputFile, pathErr).parent_path();
    if (!pathErr) {
      resolver.addSearchDirectory(programDir.generic_string());
    }
    std::string hwErr;
    auto hw = resolver.resolve(spec.platformName, spec.boardName, &hwErr);
    if (!hw) {
      errorMsg = hwErr;
      return false;
    }

    std::filesystem::path hexPath =
        opts.outputFile.empty()
            ? (parentDir.empty() ? std::filesystem::path(stem + ".hex")
                                 : (parentDir / (stem + ".hex")))
            : std::filesystem::path(opts.outputFile);
    std::filesystem::path elfPath = hexPath;
    if (hexPath.extension() == ".elf") {
      hexPath.replace_extension(".hex");
    } else {
      elfPath.replace_extension(".elf");
    }

    outDesc.kind = "embedded";
    outDesc.system = spec.platformName;
    outDesc.board = hw->name();
    outDesc.framework = spec.frameworkName;
    outDesc.triple = hw->stringProperty("triple");
    outDesc.cpu = hw->stringProperty("cpu");
    outDesc.artifact = elfPath.generic_string();
    outDesc.hex = hexPath.generic_string();
    outDesc.flashRunner = resolveUploadMethod(*hw, opts);
    outDesc.baud =
        uploadMethodInt(*hw, "avrdude", "baud", "").value_or(115200);
    if (!opts.serialPort.empty()) {
      outDesc.port = opts.serialPort;
    } else {
      std::string portErr;
      outDesc.port = detectBoardSerialPort(*hw, portErr);
    }
    outDesc.monitorType =
        (outDesc.flashRunner == "probe-rs") ? "rtt" : "serial";
    outDesc.chip =
        uploadMethodString(*hw, "probeRs", "chip", "", outDesc.cpu);
    outDesc.svd = hw->blockString("debug", "svd", "");
    std::string explicitAdapter = hw->blockString("debug", "adapter", "");
    if (!explicitAdapter.empty()) {
      outDesc.debugAdapter = explicitAdapter;
    } else if (outDesc.flashRunner == "probe-rs") {
      outDesc.debugAdapter = "probe-rs";
    } else if (outDesc.flashRunner == "openocd" ||
               outDesc.triple.find("thumb") != std::string::npos ||
               outDesc.triple.find("arm") != std::string::npos) {
      outDesc.debugAdapter = "cortex-debug";
    } else if (outDesc.triple == "avr") {
      outDesc.debugAdapter = "avr-gdb";
    } else {
      outDesc.debugAdapter = "none";
    }
    return true;
  }

  // Desktop / JIT target (no hardware board).
  std::filesystem::path objPath =
      opts.outputFile.empty()
          ? (parentDir.empty() ? std::filesystem::path(stem + ".o")
                               : (parentDir / (stem + ".o")))
          : std::filesystem::path(opts.outputFile);
  outDesc.kind = "desktop";
  outDesc.system = "JIT";
  outDesc.board = "Host";
  outDesc.framework = "JIT";
  outDesc.triple = "host";
  outDesc.cpu = "generic";
  outDesc.artifact = objPath.generic_string();
  outDesc.hex.clear();
  outDesc.flashRunner = "none";
  outDesc.port.clear();
  outDesc.baud = 0;
  outDesc.monitorType = "stdio";
  outDesc.debugAdapter = defaultDesktopDebugAdapter();
  outDesc.chip.clear();
  outDesc.svd.clear();
  return true;
}

std::string targetDescriptorToJson(const TargetDescriptor &desc) {
  std::ostringstream ss;
  ss << "{\n"
     << "  \"kind\": \"" << escapeJsonString(desc.kind) << "\",\n"
     << "  \"system\": \"" << escapeJsonString(desc.system) << "\",\n"
     << "  \"board\": \"" << escapeJsonString(desc.board) << "\",\n"
     << "  \"framework\": \"" << escapeJsonString(desc.framework) << "\",\n"
     << "  \"triple\": \"" << escapeJsonString(desc.triple) << "\",\n"
     << "  \"cpu\": \"" << escapeJsonString(desc.cpu) << "\",\n"
     << "  \"artifact\": \"" << escapeJsonString(desc.artifact) << "\",\n"
     << "  \"hex\": \"" << escapeJsonString(desc.hex) << "\",\n"
     << "  \"flash\": {\n"
     << "    \"runner\": \"" << escapeJsonString(desc.flashRunner) << "\",\n"
     << "    \"port\": \"" << escapeJsonString(desc.port) << "\",\n"
     << "    \"baud\": " << desc.baud << "\n"
     << "  },\n"
     << "  \"monitor\": {\n"
     << "    \"type\": \"" << escapeJsonString(desc.monitorType) << "\",\n"
     << "    \"port\": \"" << escapeJsonString(desc.port) << "\",\n"
     << "    \"baud\": " << desc.baud << "\n"
     << "  },\n"
     << "  \"debug\": {\n"
     << "    \"adapter\": \"" << escapeJsonString(desc.debugAdapter) << "\",\n"
     << "    \"program\": \"" << escapeJsonString(desc.artifact) << "\",\n"
     << "    \"chip\": \"" << escapeJsonString(desc.chip) << "\",\n"
     << "    \"svd\": \"" << escapeJsonString(desc.svd) << "\"\n"
     << "  }\n"
     << "}\n";
  return ss.str();
}

bool buildDesktopTarget(const BoardBuildOptions &opts, std::ostream &out,
                        std::ostream &err) {
  if (opts.inputFile.empty() || !std::filesystem::exists(opts.inputFile)) {
    err << "stride: error: input file not found: '" << opts.inputFile << "'\n";
    return false;
  }

  const std::string strideRoot =
      opts.strideRoot.empty() ? ASTFunctions::getDefaultStrideRoot()
                              : opts.strideRoot;
  std::filesystem::path inPath(opts.inputFile);
  std::string stem = inPath.stem().string();
  std::filesystem::path outObj =
      opts.outputFile.empty()
          ? (inPath.parent_path().empty() ? std::filesystem::path(stem + ".o")
                                          : (inPath.parent_path() / (stem + ".o")))
          : std::filesystem::path(opts.outputFile);
  std::filesystem::path outHdr = outObj;
  outHdr.replace_extension(".h");

  std::error_code ec;
  if (outObj.has_parent_path()) {
    std::filesystem::create_directories(outObj.parent_path(), ec);
  }

  StrideEnvironment env(strideRoot);
  env.setOptimizationLevel(opts.optimizationLevel);
  env.setStrict(opts.strict);
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

  if (!env.generateIr(opts.inputFile, false)) {
    err << "stride: error: Stride desktop compilation failed\n";
    return false;
  }
  if (!env.emitObjectFile(outObj.string(), "", "", "", "", "")) {
    err << "stride: error: failed to emit host object file: " << outObj.string()
        << "\n";
    return false;
  }

  std::string hdr = env.generateCHeaderString();
  if (!hdr.empty()) {
    std::ofstream hf(outHdr, std::ios::binary);
    if (hf) {
      hf << hdr;
    }
  }

  TargetDescriptor desc;
  std::string descErr;
  if (inspectTargetDescriptor(opts, desc, descErr)) {
    std::filesystem::path manifestPath = outObj;
    manifestPath.replace_extension(".target.json");
    writeTargetManifestFile(manifestPath, desc, opts.verbose, out, err);
  }

  out << "Built desktop target: " << outObj.string() << "\n";
  return true;
}

int runUnifiedCommand(const std::vector<std::string> &args,
                      const std::string &strideRoot, const std::string &mode,
                      std::ostream &out, std::ostream &err) {
  BoardBuildOptions opts;
  bool showHelp = false;
  if (!parseUnifiedOptions(args, strideRoot, mode, opts, showHelp, err)) {
    return 1;
  }
  if (showHelp) {
    printUnifiedHelp(out, mode);
    return 0;
  }
  if (opts.inputFile.empty()) {
    err << "stride: error: no input file specified\n";
    return 1;
  }
  if (!std::filesystem::exists(opts.inputFile)) {
    err << "stride: error: input file not found: '" << opts.inputFile << "'\n";
    return 1;
  }

  BoardSpec spec;
  std::string inspectErr;
  const bool isBoard = inspectProgramBoard(
      opts.inputFile, opts.strideRoot, opts.boardOverride, spec, inspectErr);

  if (isBoard) {
    if (!buildBoardFirmware(opts, out, err)) {
      return 1;
    }
    if (mode == "run") {
      TargetDescriptor desc;
      std::string descErr;
      if (inspectTargetDescriptor(opts, desc, descErr) && !desc.port.empty()) {
        out << "Monitor ready (" << desc.monitorType << "): " << desc.port
            << " @ " << desc.baud << " baud\n";
      }
    }
    return 0;
  }

  // Desktop / JIT target
  if (mode == "flash") {
    out << "Note: '" << opts.inputFile
        << "' targets desktop/JIT (no hardware board); building target.\n";
  }
  if (!buildDesktopTarget(opts, out, err)) {
    return 1;
  }
  if (mode == "run") {
    const std::string effectiveRoot =
        opts.strideRoot.empty() ? ASTFunctions::getDefaultStrideRoot()
                                : opts.strideRoot;
    StrideEnvironment env(effectiveRoot);
    env.setOptimizationLevel(opts.optimizationLevel);
    env.setStrict(opts.strict);
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
        return 1;
      }
    }

    if (!env.generateIr(opts.inputFile, false)) {
      err << "stride: error: failed to compile '" << opts.inputFile
          << "' for JIT execution\n";
      return 1;
    }
    if (!env.compileInMemory()) {
      err << "stride: error: failed to JIT-compile '" << opts.inputFile
          << "'\n";
      return 1;
    }
    out << "Running desktop JIT target: " << opts.inputFile << "\n";
  }
  return 0;
}

int runDevicesCommand(const std::vector<std::string> &args, std::ostream &out,
                      std::ostream &err) {
  bool json = false;
  for (const auto &arg : args) {
    if (arg == "-h" || arg == "--help") {
      out << "Usage: stride devices [--json]\n\n"
          << "List connected serial/hardware devices and desktop host target.\n";
      return 0;
    } else if (arg == "--json") {
      json = true;
    } else {
      err << "stride: error: unknown option '" << arg
          << "' for 'stride devices'\n";
      return 1;
    }
  }

  std::vector<SerialDeviceInfo> serials = enumerateSerialDevices();
  if (json) {
    out << "{\n  \"devices\": [\n"
        << "    { \"kind\": \"desktop\", \"id\": \"localhost\", \"name\": "
           "\"Host Desktop\" }";
    for (const auto &dev : serials) {
      out << ",\n    { \"kind\": \"serial\", \"port\": \""
          << escapeJsonString(dev.port) << "\", \"vidPid\": \""
          << escapeJsonString(dev.vidPid) << "\" }";
    }
    out << "\n  ]\n}\n";
  } else {
    out << "Available Targets & Devices:\n"
        << "  - localhost (Host Desktop)\n";
    if (serials.empty()) {
      out << "  (No active serial ports detected)\n";
    } else {
      for (const auto &dev : serials) {
        out << "  - " << dev.port;
        if (!dev.vidPid.empty()) {
          out << " [USB " << dev.vidPid << "]";
        }
        out << "\n";
      }
    }
  }
  return 0;
}

int runDebugConfigCommand(const std::vector<std::string> &args,
                          const std::string &strideRoot, std::ostream &out,
                          std::ostream &err) {
  BoardBuildOptions opts;
  opts.strideRoot = strideRoot;
  bool json = false;

  for (size_t i = 0; i < args.size(); ++i) {
    const std::string &arg = args[i];
    if (arg == "-h" || arg == "--help") {
      out << "Usage: stride debug-config [--json] [options] <file.stride>\n\n"
          << "Inspect target deployment and debug configuration for a Stride "
             "program.\n";
      return 0;
    } else if (arg == "--json") {
      json = true;
    } else if (arg.rfind("--boardOverride=", 0) == 0) {
      opts.boardOverride = arg.substr(16);
    } else if (arg.rfind("--port=", 0) == 0) {
      opts.serialPort = arg.substr(7);
    } else if (arg.rfind("--strideroot=", 0) == 0) {
      opts.strideRoot = arg.substr(13);
    } else if (arg == "-o" && i + 1 < args.size()) {
      opts.outputFile = args[++i];
    } else if (!arg.empty() && arg[0] == '-') {
      err << "stride: error: unknown option '" << arg
          << "' for 'stride debug-config'\n";
      return 1;
    } else {
      opts.inputFile = arg;
    }
  }

  if (opts.inputFile.empty()) {
    err << "stride: error: no input file specified\n";
    return 1;
  }

  TargetDescriptor desc;
  std::string errMsg;
  if (!inspectTargetDescriptor(opts, desc, errMsg)) {
    err << "stride: error: " << errMsg << "\n";
    return 1;
  }

  if (json) {
    out << targetDescriptorToJson(desc);
  } else {
    out << "Target: " << desc.kind << " (" << desc.board << " / " << desc.system
        << ")\n"
        << "Artifact: " << desc.artifact << "\n"
        << "Flash runner: " << desc.flashRunner << "\n"
        << "Monitor: " << desc.monitorType
        << (desc.port.empty() ? "" : (" on " + desc.port)) << "\n"
        << "Debug adapter: " << desc.debugAdapter << "\n";
  }
  return 0;
}

} // namespace strd

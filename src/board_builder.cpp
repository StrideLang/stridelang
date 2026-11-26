#include "stridec/board_builder.hpp"

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/platformnode.h"
#include "stride/utils/astquery.h"
#include "stride/utils/featureresolver.h"
#include "stride/stridegen/hardwareresolver.hpp"
#include "stride/stridegen/stridesystem.hpp"
#include "stride/stridegen/toolmanager.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/cachemanager.h"

#ifdef STRIDEC_EMBEDDED_RUNTIME
#include "stridec/embedded_runtime.hpp"
#include "stridec/stm32_board.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace strd {

namespace {

// Where a framework's firmware is built from: the first framework in its
// `inherits` chain with a runtime/CMakeLists.txt, and the first
// `firmwareBuilder` the chain declares (e.g. "Stm32").
struct FrameworkRuntime {
  std::filesystem::path dir;
  std::string builder;
};

FrameworkRuntime findFrameworkRuntime(const std::string &strideRoot,
                                      std::string name, std::string version) {
  FrameworkRuntime result;
  std::set<std::string> visited;
  while (!name.empty() && visited.insert(name).second) {
    const std::filesystem::path root =
        std::filesystem::path(strideRoot) / "frameworks" / name / version;
    if (result.dir.empty() &&
        std::filesystem::exists(root / "runtime" / "CMakeLists.txt")) {
      result.dir = root / "runtime";
    }
    const std::string descriptionFile =
        (root / "platformlib" / "Framework.stride").string();
    ASTNode tree = std::filesystem::exists(descriptionFile)
                       ? AST::parseFile(descriptionFile.c_str())
                       : nullptr;
    std::shared_ptr<DeclarationNode> description;
    for (const auto &node : tree ? tree->getChildren() : std::vector<ASTNode>{}) {
      if (node && node->getNodeType() == AST::Declaration &&
          std::static_pointer_cast<DeclarationNode>(node)->getEntityType() ==
              "_frameworkDescription") {
        description = std::static_pointer_cast<DeclarationNode>(node);
        break;
      }
    }
    if (!description) {
      break;
    }
    if (result.builder.empty()) {
      result.builder =
          ASTQuery::valueText(description->getPropertyValue("firmwareBuilder"));
    }
    name = ASTQuery::valueText(description->getPropertyValue("inherits"));
    version =
        ASTQuery::valueText(description->getPropertyValue("inheritsVersion"));
  }
  return result;
}

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

bool inspectProgramBoard(const BoardBuildOptions &opts, BoardSpec &outSpec,
                         std::string &errorMsg) {
  ASTNode tree = AST::parseFile(opts.inputFile.c_str());
  if (!tree) {
    errorMsg = "failed to parse '" + opts.inputFile + "'";
    return false;
  }

  FeatureConfig featConfig;
  featConfig.useDefaultFeatures = !opts.noDefaultFeatures;
  for (const auto &f : opts.featureToggles) {
    const auto sep = f.find("::");
    if (sep != std::string::npos) {
      featConfig.exclusiveChoices[f.substr(0, sep)] = f.substr(sep + 2);
    }
    featConfig.enabledFeatures.insert(f);
  }
  for (const auto &d : opts.disabledFeatures) {
    featConfig.disabledFeatures.insert(d);
  }
  FeatureResolver featResolver(tree, featConfig);
  if (!featResolver.process()) {
    const auto &errs = featResolver.getErrors();
    errorMsg = errs.empty() ? "feature resolution failed"
                            : errs.front().getErrorText();
    return false;
  }

  const std::string effectiveRoot =
      opts.strideRoot.empty() ? ASTFunctions::getDefaultStrideRoot()
                              : opts.strideRoot;

  auto systems = ASTQuery::getSystemNodes(tree);
  if (systems.empty()) {
    if (!opts.boardOverride.empty() || !opts.systemOverride.empty()) {
      std::string plat = opts.systemOverride;
      std::string board = opts.boardOverride;
      if (plat.empty() && !board.empty()) {
        HardwareResolver resolver(effectiveRoot);
        std::error_code platEc;
        const std::filesystem::path platRoot =
            std::filesystem::path(effectiveRoot) / "platforms";
        if (std::filesystem::is_directory(platRoot, platEc)) {
          for (const auto &entry :
               std::filesystem::directory_iterator(platRoot, platEc)) {
            if (!entry.is_directory(platEc)) {
              continue;
            }
            const std::string candidate = entry.path().filename().string();
            if (resolver.resolve(candidate, board)) {
              plat = candidate;
              break;
            }
          }
        }
      }
      if (board.empty() && !plat.empty()) {
        const std::filesystem::path platFile =
            std::filesystem::path(effectiveRoot) / "platforms" / plat /
            "Platform.stride";
        if (std::filesystem::exists(platFile)) {
          if (ASTNode platTree = AST::parseFile(platFile.string().c_str())) {
            for (const auto &child : platTree->getChildren()) {
              if (child && child->getNodeType() == AST::Declaration) {
                auto decl = std::static_pointer_cast<DeclarationNode>(child);
                if (decl->getEntityType() == "platform") {
                  board = ASTQuery::valueText(
                      decl->getPropertyValue("defaultDevice"));
                  break;
                }
              }
            }
          }
        }
      }
      if (plat.empty() || board.empty()) {
        return false;
      }
      outSpec.platformName = plat;
      outSpec.boardName = board;
      outSpec.frameworkName = plat;
      outSpec.frameworkVersion = "1.0";
      return true;
    }
    return false;
  }

  const auto &sysNode = systems.front();
  std::string systemName =
      opts.systemOverride.empty() ? sysNode->platformName() : opts.systemOverride;
  std::string targetBoard = opts.boardOverride;
  if (targetBoard.empty()) {
    auto hw = sysNode->hwPlatforms();
    if (!hw.empty()) {
      targetBoard = hw.front();
    }
  }
  if (targetBoard.empty()) {
    return false;
  }

  outSpec.platformName = systemName;
  outSpec.boardName = targetBoard;
  outSpec.frameworkName = systemName;
  outSpec.frameworkVersion =
      (opts.systemOverride.empty() && sysNode->majorVersion() >= 0 &&
       sysNode->minorVersion() >= 0)
          ? (std::to_string(sysNode->majorVersion()) + "." +
             std::to_string(sysNode->minorVersion()))
          : "1.0";

  StrideSystem sys(effectiveRoot, systemName,
                   opts.systemOverride.empty() ? sysNode->majorVersion() : 1,
                   opts.systemOverride.empty() ? sysNode->minorVersion() : 0,
                   {}, featConfig);
  auto frameworks = sys.getFrameworks();
  if (!frameworks.empty()) {
    outSpec.frameworkName = frameworks.front()->getFramework();
    outSpec.frameworkVersion = frameworks.front()->getFrameworkVersion();
  }
  return true;
}

bool inspectProgramBoard(const std::string &inputFile,
                         const std::string &strideRoot,
                         const std::string &boardOverride,
                         BoardSpec &outSpec, std::string &errorMsg) {
  BoardBuildOptions opts;
  opts.inputFile = inputFile;
  opts.strideRoot = strideRoot;
  opts.boardOverride = boardOverride;
  return inspectProgramBoard(opts, outSpec, errorMsg);
}

std::string generateBoardHeader(const ResolvedHardware &hw,
                                std::string &errorMsg,
                                const ASTNode &resolvedTree) {
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
  const int64_t baud = hw.flasher("avrdude")
                           .value_or(Flasher{})
                           .integer("baud")
                           .value_or(115200);

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

  if (resolvedTree) {
    auto stripQ = [](std::string n) {
      size_t pos = n.rfind("::");
      return pos != std::string::npos ? n.substr(pos + 2) : n;
    };
    std::map<std::string, std::shared_ptr<DeclarationNode>> resources;
    std::map<std::string, std::shared_ptr<DeclarationNode>> gpioPins;
    std::vector<std::shared_ptr<DeclarationNode>> isrDomains;
    for (const auto &node : resolvedTree->getChildren()) {
      if (node->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      const std::string &et = decl->getEntityType();
      if (et == "resource" || et == "domainResource") {
        resources[decl->getName()] = decl;
      } else if (et == "gpioPin") {
        gpioPins[decl->getName()] = decl;
      } else if (et == "_domainDefinition" || et == "gameDefinition") {
        auto execProp = decl->getPropertyValue("execution");
        if (execProp &&
            (stripQ(ASTQuery::getNodeName(execProp)) == "ExecutionInterrupt" || (execProp->getCompilerProperty("resolvedFrom") && stripQ(ASTQuery::getNodeName(execProp->getCompilerProperty("resolvedFrom"))) == "ExecutionInterrupt") || (execProp->getNodeType() == AST::Int && std::static_pointer_cast<ValueNode>(execProp)->getIntValue() == 1))) {
          isrDomains.push_back(decl);
        }
      }
    }

    std::string isrDeclsMacro;
    std::string isrInitMacro;
    for (const auto &domDecl : isrDomains) {
      std::string domName = domDecl->getName();
      std::string isrVec;
      if (auto vp = domDecl->getCompilerProperty("isrVector")) {
        if (vp->getNodeType() == AST::String) {
          isrVec = std::static_pointer_cast<ValueNode>(vp)->getStringValue();
        }
      }
      if (auto reqProp = domDecl->getPropertyValue("requiresResources")) {
        if (reqProp->getNodeType() == AST::List) {
          for (const auto &item : reqProp->getChildren()) {
            std::string rName = stripQ(ASTQuery::getNodeName(item));
            auto rIt = resources.find(rName);
            if (rIt != resources.end() && rIt->second) {
              auto vecProp = rIt->second->getPropertyValue("isrVector");
              if (vecProp && vecProp->getNodeType() == AST::String) {
                isrVec = std::static_pointer_cast<ValueNode>(vecProp)
                             ->getStringValue();
                if (!isrVec.empty()) {
                  break;
                }
              }
            }
          }
        }
      }
      if (isrVec.empty()) {
        if (auto trigProp = domDecl->getPropertyValue("trigger")) {
          std::string trigName = stripQ(ASTQuery::getNodeName(trigProp));
          auto pIt = gpioPins.find(trigName);
          if (pIt != gpioPins.end() && pIt->second) {
            if (auto vp = pIt->second->getCompilerProperty("isrVector")) {
              if (vp->getNodeType() == AST::String) {
                isrVec =
                    std::static_pointer_cast<ValueNode>(vp)->getStringValue();
              }
            }
          }
        }
      }
      if (isrVec.empty()) {
        continue;
      }

      std::string vecMacro = toUpper(isrVec);
      ss << "#define STRIDE_ISR_" << vecMacro << " " << domName << "\n";

      isrDeclsMacro +=
          "int32_t " + domName + "_init(void *state) __attribute__((weak)); \\\n";
      isrDeclsMacro +=
          "int32_t " + domName + "_process(void *state) __attribute__((weak)); \\\n";
      isrDeclsMacro +=
          "extern uint8_t __stride_" + domName + "_state[] __attribute__((weak)); \\\n";
      isrDeclsMacro += "ISR(" + isrVec + ") { \\\n"
                       "  if (" + domName + "_process) { \\\n"
                       "    " + domName + "_process(__stride_" + domName + "_state); \\\n"
                       "  } \\\n"
                       "} \\\n";

      isrInitMacro += "if (" + domName + "_init) { \\\n"
                      "  " + domName + "_init(__stride_" + domName + "_state); \\\n"
                      "} \\\n";
      if (isrVec == "TIMER1_COMPA_vect") {
        int64_t rateHz = 8000;
        if (auto rateProp = domDecl->getPropertyValue("rate")) {
          if (rateProp->getNodeType() == AST::Int ||
              rateProp->getNodeType() == AST::Real) {
            rateHz = static_cast<int64_t>(
                std::static_pointer_cast<ValueNode>(rateProp)->toReal());
          }
        }
        if (rateHz <= 0) {
          rateHz = 1000;
        }
        isrInitMacro +=
            "TCCR1A = 0; \\\n"
            "TCCR1B = (1 << WGM12) | (1 << CS11); \\\n"
            "OCR1A = (uint16_t)(F_CPU / 8UL / " +
            std::to_string(rateHz) + "UL - 1UL); \\\n"
            "TIMSK1 = (1 << OCIE1A); \\\n";
      }
    }
    if (!isrDeclsMacro.empty()) {
      ss << "#define STRIDE_ISR_DECLARATIONS \\\n" << isrDeclsMacro << "\n";
    }
    if (!isrInitMacro.empty()) {
      ss << "#define STRIDE_ISR_INIT \\\n" << isrInitMacro << "\n";
    }
  }

  return ss.str();
}

std::string findTool(const std::string &toolName, const std::string &toolKey,
                     const std::string &strideRoot) {
  if (auto p = findInPath(toolName); !p.empty()) {
    return p;
  }
  if (!strideRoot.empty() && !toolKey.empty()) {
    ToolManager tm(strideRoot);
    auto it = tm.localTools.find(toolKey);
    if (it != tm.localTools.end() && !it->second.empty()) {
      std::filesystem::path toolPath(it->second);
      auto sibling = findExecutableInDir(toolPath.parent_path(), toolName);
      if (!sibling.empty()) {
        return sibling;
      }
    }
  }
  return {};
}

bool prepareStringRuntimeSources(const std::filesystem::path &workDir,
                                 std::filesystem::path &outRtDir,
                                 std::filesystem::path &outStringSrc,
                                 std::ostream &err) {
#ifdef STRIDEC_EMBEDDED_RUNTIME
  outRtDir = workDir / "stride_rt";
  const std::filesystem::path statusDir = outRtDir / "stride" / "stridejit";
  std::error_code ec;
  std::filesystem::create_directories(statusDir, ec);
  auto writeEmbedded = [&](const std::filesystem::path &path,
                           const unsigned char *bytes, size_t size) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes),
               static_cast<std::streamsize>(size));
    return static_cast<bool>(file);
  };
  outStringSrc = outRtDir / "strideruntime_string.c";
  if (!writeEmbedded(outStringSrc, kStringRuntimeSource,
                     sizeof(kStringRuntimeSource) - 1) ||
      !writeEmbedded(statusDir / "stride_status.h", kStatusHeaderSource,
                     sizeof(kStatusHeaderSource) - 1)) {
    err << "stride: error: failed to write the string runtime to "
        << outRtDir.string() << "\n";
    return false;
  }
  return true;
#else
  (void)workDir;
  (void)outRtDir;
  (void)outStringSrc;
  err << "stride: error: embedded string runtime is not available in this build\n";
  return false;
#endif
}

bool isInteractiveTerminal() {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) != 0 && _isatty(_fileno(stdout)) != 0;
#else
  return isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0;
#endif
}

std::filesystem::path userHomeDir() {
#ifdef _WIN32
  const char *home = std::getenv("USERPROFILE");
  if (!home || !*home) {
    home = std::getenv("LOCALAPPDATA");
  }
#else
  const char *home = std::getenv("HOME");
#endif
  if (!home || !*home) {
    return {};
  }
  return std::filesystem::path(home);
}

std::filesystem::path findNewestSubdir(const std::filesystem::path &parent) {
  std::error_code ec;
  if (!std::filesystem::is_directory(parent, ec)) {
    return {};
  }
  std::vector<std::filesystem::path> dirs;
  for (const auto &entry : std::filesystem::directory_iterator(parent, ec)) {
    if (entry.is_directory(ec)) {
      dirs.push_back(entry.path());
    }
  }
  if (dirs.empty()) {
    return {};
  }
  std::sort(dirs.begin(), dirs.end());
  return dirs.back();
}

std::filesystem::path findPicoVsCodeSdk(std::filesystem::path &outVsCodeRoot) {
  const std::filesystem::path home = userHomeDir();
  if (home.empty()) {
    return {};
  }
  const std::filesystem::path vsCodeRoot = home / ".pico-sdk";
  const std::filesystem::path sdk = findNewestSubdir(vsCodeRoot / "sdk");
  if (!sdk.empty() &&
      std::filesystem::exists(sdk / "external" / "pico_sdk_import.cmake")) {
    outVsCodeRoot = vsCodeRoot;
    return sdk;
  }
  return {};
}

std::string findPicoVsCodeTool(const std::filesystem::path &vsCodeRoot,
                               const std::string &toolDir,
                               const std::string &exeName) {
  if (vsCodeRoot.empty()) {
    return {};
  }
  const std::filesystem::path verDir = findNewestSubdir(vsCodeRoot / toolDir);
  if (verDir.empty()) {
    return {};
  }
  for (const auto &cand : {verDir, verDir / "bin", verDir / exeName}) {
    if (auto p = findExecutableInDir(cand, exeName); !p.empty()) {
      return p;
    }
  }
  return {};
}

std::filesystem::path strideToolsCacheDir() {
#ifdef _WIN32
  const char *home = std::getenv("USERPROFILE");
  if (!home || !*home) {
    home = std::getenv("LOCALAPPDATA");
  }
#else
  const char *home = std::getenv("HOME");
#endif
  if (!home || !*home) {
    return std::filesystem::temp_directory_path() / "stride_tools";
  }
  return std::filesystem::path(home) / ".stride" / "tools";
}

std::string findCachedOpenOcd(const std::filesystem::path &rootDir) {
  std::error_code ec;
  if (!std::filesystem::exists(rootDir, ec)) {
    return {};
  }
#ifdef _WIN32
  const std::string exeName = "openocd.exe";
#else
  const std::string exeName = "openocd";
#endif
  for (auto it = std::filesystem::recursive_directory_iterator(rootDir, ec);
       it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) {
      break;
    }
    if (it->is_regular_file(ec) && it->path().filename() == exeName) {
      return it->path().string();
    }
  }
  return {};
}

// Reusable across any platform/board that declares upload.openocdTarget.
// Locates OpenOCD in PATH / ToolManager / ~/.stride/tools/openocd, or downloads
// the version specified by the board's upload.openocdUrl* properties.
std::string ensureOpenOcd(const ResolvedHardware &hw,
                          const BoardBuildOptions &opts,
                          const std::string &strideRoot, std::ostream &out,
                          std::ostream &err) {
  if (auto p = findTool("openocd", "OpenOCD", strideRoot); !p.empty()) {
    return p;
  }

  const std::filesystem::path cacheDir = strideToolsCacheDir() / "openocd";
  {
    std::filesystem::path vsCodeRoot;
    if (!findPicoVsCodeSdk(vsCodeRoot).empty()) {
      if (auto p = findPicoVsCodeTool(vsCodeRoot, "openocd", "openocd"); !p.empty()) {
        return p;
      }
    }
  }
  if (const char *picoSdkEnv = std::getenv("PICO_SDK_PATH");
      picoSdkEnv && *picoSdkEnv) {
    std::filesystem::path sdk(picoSdkEnv);
    sdk = sdk.lexically_normal();
    if (sdk.filename().empty() && sdk.has_parent_path()) {
      sdk = sdk.parent_path();
    }
    std::filesystem::path root =
        (sdk.filename() == "pico-sdk") ? sdk.parent_path() : sdk;
    for (const auto &rel : {"openocd", "openocd/bin"}) {
      if (auto p = findExecutableInDir(root / rel, "openocd"); !p.empty()) {
        return p;
      }
    }
  }
  if (auto cached = findCachedOpenOcd(cacheDir); !cached.empty()) {
    return cached;
  }

#ifdef _WIN32
  const char *urlKey = "windows";
  const char *archiveExt = ".zip";
#elif defined(__APPLE__)
  const char *urlKey = "macos";
  const char *archiveExt = ".tar.gz";
#else
  const char *urlKey = "linux";
  const char *archiveExt = ".tar.gz";
#endif

  const Flasher openocd = hw.flasher("openocd").value_or(Flasher{});
  const std::string url = openocd.recordString("download", urlKey);
  if (url.empty()) {
    err << "stride: error: openocd not found and board '" << hw.name()
        << "' gives no OpenOCD download for this OS (flashers: openocd OpenOCD { download: { " << urlKey << ": } })\n";
    return {};
  }

  const std::string minVer = openocd.string("minVersion");
  if (!opts.noPrompt && isInteractiveTerminal()) {
    out << "OpenOCD" << (minVer.empty() ? "" : (" (>= " + minVer + ")"))
        << " is not installed.\n"
        << "Download from " << url << " into " << cacheDir.string()
        << "? [Y/n] " << std::flush;
    std::string reply;
    std::getline(std::cin, reply);
    if (!reply.empty() && reply[0] != 'y' && reply[0] != 'Y') {
      err << "stride: error: OpenOCD download cancelled by user\n";
      return {};
    }
  } else {
    out << "Downloading OpenOCD from " << url << "...\n";
  }

  const std::string curlExe = findInPath("curl");
  const std::string tarExe = findInPath("tar");
  if (curlExe.empty() || tarExe.empty()) {
    err << "stride: error: 'curl' and 'tar' are required to download OpenOCD automatically\n";
    return {};
  }

  std::error_code ec;
  std::filesystem::create_directories(cacheDir, ec);
  const std::filesystem::path archivePath =
      cacheDir / (std::string("openocd_download") + archiveExt);

  if (runSubprocess(curlExe, {"-fsSL", "-L", url, "-o", archivePath.string()},
                    opts.verbose, out) != 0) {
    err << "stride: error: failed to download OpenOCD from " << url << "\n";
    return {};
  }
#ifdef _WIN32
  if (std::string(archiveExt) == ".zip") {
    const std::string psExe = findInPath("powershell");
    if (psExe.empty()) {
      err << "stride: error: powershell is required to extract " << archivePath.string() << "\n";
      return {};
    }
    const std::string psCmd =
        "Expand-Archive -LiteralPath '" + archivePath.string() +
        "' -DestinationPath '" + cacheDir.string() + "' -Force";
    if (runSubprocess(psExe, {"-NoProfile", "-Command", psCmd}, opts.verbose, out) != 0) {
      err << "stride: error: failed to extract " << archivePath.string() << "\n";
      return {};
    }
  } else
#endif
  if (runSubprocess(tarExe, {"-xf", archivePath.string(), "-C", cacheDir.string()},
                    opts.verbose, out) != 0) {
    err << "stride: error: failed to extract " << archivePath.string() << "\n";
    return {};
  }
  std::filesystem::remove(archivePath, ec);

  std::string installed = findCachedOpenOcd(cacheDir);
  if (installed.empty()) {
    err << "stride: error: OpenOCD archive extracted to " << cacheDir.string()
        << " but openocd executable was not found\n";
    return {};
  }
  out << "Installed OpenOCD to " << installed << "\n";
  return installed;
}

bool flashWithOpenOcd(const ResolvedHardware &hw,
                      const std::filesystem::path &elfPath,
                      const BoardBuildOptions &opts,
                      const std::string &strideRoot, std::ostream &out,
                      std::ostream &err) {
  // The device's and board's `openocd OpenOCD { ... }` entries, merged.
  const Flasher openocd = hw.flasher("openocd").value_or(Flasher{});
  const std::string boardScript = openocd.string("boardScript");
  const std::string targetCfg = openocd.string("target");
  if (boardScript.empty() && targetCfg.empty()) {
    err << "stride: error: '" << hw.name()
        << "' has no OpenOCD target script; add flashers: [openocd OpenOCD "
           "{ target: \"target/<chip>.cfg\" }] to its device or board\n";
    return false;
  }
  const std::string ifaceCfg =
      !opts.probeInterface.empty()
          ? opts.probeInterface
          : openocd.string("interface", "interface/cmsis-dap.cfg");
  const std::string transportCfg = openocd.string("transport");
  const int64_t speedKhz = openocd.integer("speed").value_or(0);

  const std::string openocdExe = ensureOpenOcd(hw, opts, strideRoot, out, err);
  if (openocdExe.empty()) {
    return false;
  }

  const std::string elfGeneric = elfPath.generic_string();
  std::vector<std::string> args;
  const std::filesystem::path openocdDir =
      std::filesystem::path(openocdExe).parent_path();
  if (std::filesystem::exists(openocdDir / "scripts")) {
    args.push_back("-s");
    args.push_back((openocdDir / "scripts").generic_string());
  } else if (std::filesystem::exists(openocdDir.parent_path() / "share" /
                                     "openocd" / "scripts")) {
    args.push_back("-s");
    args.push_back(
        (openocdDir.parent_path() / "share" / "openocd" / "scripts")
            .generic_string());
  }
  if (!boardScript.empty() && opts.probeInterface.empty()) {
    args.push_back("-f");
    args.push_back(boardScript);
  } else {
    if (!ifaceCfg.empty()) {
      args.push_back("-f");
      args.push_back(ifaceCfg);
    }
    if (!transportCfg.empty()) {
      args.push_back("-c");
      args.push_back("transport select " + transportCfg);
    }
    if (!targetCfg.empty()) {
      args.push_back("-f");
      args.push_back(targetCfg);
    }
  }
  if (speedKhz > 0) {
    args.insert(args.end(), {"-c", "adapter speed " + std::to_string(speedKhz)});
  }
  std::string program = "program " + elfGeneric;
  if (openocd.flag("verify", true)) {
    program += " verify";
  }
  if (openocd.flag("reset", true)) {
    program += " reset";
  }
  args.insert(args.end(), {"-c", program + " exit"});

  if (runSubprocess(openocdExe, args, opts.verbose, out) != 0) {
    err << "stride: error: OpenOCD SWD flash failed (check probe connection and target)\n";
#ifdef _WIN32
    const std::string plat = toLower(hw.stringProperty("platform"));
    if ((plat.rfind("stm32", 0) == 0 || plat == "st") &&
        toLower(ifaceCfg).find("stlink") != std::string::npos) {
      err << "hint: on Windows, OpenOCD requires a WinUSB driver for 'ST-Link Debug' (Interface 0).\n"
          << "      Install STM32CubeCLT / STSW-LINK009 (or the STM32 VS Code Extension),\n"
          << "      or use Zadig to install WinUSB on 'ST-Link Debug (Interface 0)'.\n";
    }
#endif
    return false;
  }
  out << "Flashed " << elfPath.string() << " to " << hw.name()
      << " via OpenOCD SWD\n";
  return true;
}

bool flashPicoWithPicotool(const std::filesystem::path &uf2Path,
                           const ResolvedHardware &hw,
                           const BoardBuildOptions &opts,
                           const std::string &picotoolExe, std::ostream &out,
                           std::ostream &err) {
  auto tryLoad = [&]() {
    return runSubprocess(picotoolExe, {"load", "-f", "-x", uf2Path.string()},
                         opts.verbose, out) == 0;
  };

  if (tryLoad()) {
    out << "Uploaded " << uf2Path.string() << " to " << hw.name() << "\n";
    return true;
  }

  if (opts.noPrompt || !isInteractiveTerminal()) {
    err << "stride: error: no Raspberry Pi Pico detected in BOOTSEL or USB-reset mode.\n"
        << "Hold BOOTSEL while connecting USB, or use a Debug Probe (--probe).\n";
    return false;
  }

  const std::string customPrompt =
      hw.flasher("picotool").value_or(Flasher{}).string("prepare");
  out << "\nNo " << hw.name() << " detected in bootloader/USB-reset mode.\n"
      << "To prepare " << hw.name() << " for USB flashing:\n  "
      << (customPrompt.empty()
              ? "Put the board into bootloader mode and reconnect USB."
              : customPrompt)
      << "\nWaiting for board (press Ctrl+C to cancel)..." << std::endl;

  constexpr int kMaxWaitSeconds = 60;
  for (int i = 0; i < kMaxWaitSeconds * 2; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (runSubprocess(picotoolExe, {"info"}, /*verbose=*/false, out) == 0) {
      if (tryLoad()) {
        out << "Uploaded " << uf2Path.string() << " to " << hw.name() << "\n";
        return true;
      }
      break;
    }
  }
  err << "stride: error: timed out waiting for " << hw.name()
      << " in BOOTSEL mode\n";
  return false;
}

bool buildAndUploadPicoFirmware(const BoardBuildOptions &opts,
                                const BoardSpec &spec,
                                const ResolvedHardware &hw,
                                const std::string &strideRoot,
                                const std::filesystem::path &workDir,
                                const std::filesystem::path &progObj,
                                const std::filesystem::path &outPath,
                                const std::filesystem::path &elfPath,
                                std::ostream &out, std::ostream &err) {

  const std::string picoBoard =
      hw.flasher("picotool").value_or(Flasher{}).string("board", hw.name());

  std::filesystem::path picoSdkPath;
  std::filesystem::path picoInstallerRoot;
  std::filesystem::path picoVsCodeRoot;

  if (const char *picoSdkEnv = std::getenv("PICO_SDK_PATH");
      picoSdkEnv && *picoSdkEnv && std::filesystem::exists(picoSdkEnv)) {
    picoSdkPath = std::filesystem::path(picoSdkEnv).lexically_normal();
    if (picoSdkPath.filename().empty() && picoSdkPath.has_parent_path()) {
      picoSdkPath = picoSdkPath.parent_path();
    }
    if (!std::filesystem::exists(picoSdkPath / "external" / "pico_sdk_import.cmake")) {
      if (std::filesystem::exists(picoSdkPath / "pico-sdk" / "external" / "pico_sdk_import.cmake")) {
        picoInstallerRoot = picoSdkPath;
        picoSdkPath = picoSdkPath / "pico-sdk";
      } else if (picoSdkPath.filename() == ".pico-sdk") {
        picoVsCodeRoot = picoSdkPath;
        picoSdkPath = findNewestSubdir(picoVsCodeRoot / "sdk");
      }
    } else if (picoSdkPath.filename() == "pico-sdk") {
      picoInstallerRoot = picoSdkPath.parent_path();
    } else if (picoSdkPath.parent_path().filename() == "sdk" &&
               picoSdkPath.parent_path().parent_path().filename() == ".pico-sdk") {
      picoVsCodeRoot = picoSdkPath.parent_path().parent_path();
    }
  }

  // If PICO_SDK_PATH is unset, or lacks pico2 (e.g. v1.5.1) when building for
  // RP2350, fall back to ~/.pico-sdk/sdk/<latest>.
  if (picoSdkPath.empty() ||
      !std::filesystem::exists(picoSdkPath / "external" / "pico_sdk_import.cmake") ||
      (picoBoard == "pico2" &&
       !std::filesystem::exists(picoSdkPath / "src" / "boards" / "include" / "boards" / "pico2.h"))) {
    std::filesystem::path vsCodeRoot;
    std::filesystem::path vsCodeSdk = findPicoVsCodeSdk(vsCodeRoot);
    if (!vsCodeSdk.empty()) {
      picoSdkPath = vsCodeSdk;
      picoVsCodeRoot = vsCodeRoot;
      picoInstallerRoot.clear();
    }
  }

  if (picoSdkPath.empty() ||
      !std::filesystem::exists(picoSdkPath / "external" / "pico_sdk_import.cmake")) {
    const std::string codeExe = findInPath("code");
    if (!opts.noPrompt && isInteractiveTerminal() && !codeExe.empty()) {
      out << "Raspberry Pi Pico SDK not found (PICO_SDK_PATH or ~/.pico-sdk).\n"
          << "Install the Raspberry Pi Pico VS Code extension (raspberry-pi.raspberry-pi-pico)? [Y/n] "
          << std::flush;
      std::string reply;
      std::getline(std::cin, reply);
      if (reply.empty() || reply[0] == 'y' || reply[0] == 'Y') {
        if (runSubprocess(codeExe,
                          {"--install-extension", "raspberry-pi.raspberry-pi-pico"},
                          opts.verbose, out) == 0) {
          out << "Installed raspberry-pi.raspberry-pi-pico. Open VS Code to let it download ~/.pico-sdk, or set PICO_SDK_PATH.\n";
        }
      }
    }
    err << "stride: error: pico-sdk not found (set PICO_SDK_PATH or install via ~/.pico-sdk / 'code --install-extension raspberry-pi.raspberry-pi-pico')\n";
    return false;
  }

  auto findPicoBundleTool = [&](const std::string &toolName,
                                const std::string &toolKey,
                                const std::string &subDir) -> std::string {
    if (!picoVsCodeRoot.empty()) {
      if (auto p = findPicoVsCodeTool(picoVsCodeRoot, subDir, toolName);
          !p.empty()) {
        return p;
      }
    }
    if (!picoInstallerRoot.empty()) {
      for (const auto &rel : {subDir, subDir + "/bin"}) {
        if (auto p = findExecutableInDir(picoInstallerRoot / rel, toolName);
            !p.empty()) {
          return p;
        }
      }
    }
    return findTool(toolName, toolKey, strideRoot);
  };

  const std::string cmakeExe = findPicoBundleTool("cmake", "CMake", "cmake");
  if (cmakeExe.empty()) {
    err << "stride: error: cmake not found in PATH or ToolManager\n";
    return false;
  }

  const std::string ninjaExe = findPicoBundleTool("ninja", "", "ninja");
  const std::string methodLower = resolveFlashMethod(hw, opts);
  const bool useProbeRs = (methodLower == "probe-rs");
  const bool useSwd = methodLower == "openocd";
  const bool useAuto = (methodLower == "auto");
  std::string picotoolExe = findPicoBundleTool("picotool", "Picotool", "picotool");
  if (opts.upload && !useSwd && !useProbeRs) {
    picotoolExe = findPicoBundleTool("picotool", "Picotool", "picotool");
    if (picotoolExe.empty() && !useAuto) {
      err << "stride: error: picotool not found in PATH or ToolManager (or pass --probe for SWD)\n";
      return false;
    }
  }

  const std::filesystem::path runtimeDir =
      std::filesystem::path(strideRoot) / "frameworks" / spec.frameworkName /
      spec.frameworkVersion / "runtime";
  if (!std::filesystem::exists(runtimeDir / "CMakeLists.txt")) {
    err << "stride: error: Pico runtime CMakeLists.txt not found: "
        << (runtimeDir / "CMakeLists.txt").string() << "\n";
    return false;
  }

  std::filesystem::path rtDir;
  std::filesystem::path stringSrc;
  if (!prepareStringRuntimeSources(workDir, rtDir, stringSrc, err)) {
    return false;
  }

  const std::filesystem::path cmakeBuildDir = workDir / "pico_cmake";
  if (!ninjaExe.empty() &&
      (std::filesystem::exists(cmakeBuildDir / "CMakeCache.txt") ||
       std::filesystem::exists(cmakeBuildDir / "CMakeFiles"))) {
    std::string cacheText;
    if (std::ifstream cacheIn(cmakeBuildDir / "CMakeCache.txt"); cacheIn) {
      cacheText.assign((std::istreambuf_iterator<char>(cacheIn)),
                       std::istreambuf_iterator<char>());
    }
    if (cacheText.find("CMAKE_GENERATOR:INTERNAL=Ninja") == std::string::npos ||
        cacheText.find("PICO_SDK_PATH:PATH=" + picoSdkPath.generic_string()) ==
            std::string::npos) {
      std::error_code rmEc;
      std::filesystem::remove_all(cmakeBuildDir, rmEc);
    }
  }

  std::vector<std::string> configArgs = {
      "-S",
      runtimeDir.string(),
      "-B",
      cmakeBuildDir.string(),
      "-DPICO_SDK_PATH=" + picoSdkPath.generic_string(),
      "-DPICO_BOARD=" + picoBoard,
      "-DSTRIDE_PROGRAM_OBJ=" + std::filesystem::absolute(progObj).string(),
      "-DSTRIDE_STRING_RUNTIME=" + std::filesystem::absolute(stringSrc).string(),
      "-DSTRIDE_JIT_INCLUDE_DIR=" + std::filesystem::absolute(rtDir).generic_string()};
  if (!ninjaExe.empty()) {
    configArgs.push_back("-G");
    configArgs.push_back("Ninja");
    configArgs.push_back("-DCMAKE_MAKE_PROGRAM=" +
                         std::filesystem::path(ninjaExe).generic_string());
  }
  if (!picoVsCodeRoot.empty()) {
    const std::filesystem::path tcDir = findNewestSubdir(picoVsCodeRoot / "toolchain");
    if (!tcDir.empty() && std::filesystem::exists(tcDir / "bin")) {
      configArgs.push_back("-DPICO_TOOLCHAIN_PATH=" + tcDir.generic_string());
    }
  } else if (!picoInstallerRoot.empty() &&
             std::filesystem::exists(picoInstallerRoot / "gcc-arm-none-eabi" / "bin")) {
    configArgs.push_back("-DPICO_TOOLCHAIN_PATH=" +
                         (picoInstallerRoot / "gcc-arm-none-eabi").generic_string());
  }
  if (!picotoolExe.empty()) {
    const std::filesystem::path picotoolDir =
        std::filesystem::path(picotoolExe).parent_path();
    if (std::filesystem::exists(picotoolDir / "picotoolConfig.cmake")) {
      configArgs.push_back("-Dpicotool_DIR=" + picotoolDir.generic_string());
    }
  }
  if (runSubprocess(cmakeExe, configArgs, opts.verbose, out) != 0) {
    err << "stride: error: pico-sdk CMake configure failed\n";
    return false;
  }

  if (runSubprocess(cmakeExe, {"--build", cmakeBuildDir.string(), "--parallel"},
                    opts.verbose, out) != 0) {
    err << "stride: error: pico-sdk build failed\n";
    return false;
  }

  const std::filesystem::path builtUf2 = cmakeBuildDir / "stride_firmware.uf2";
  const std::filesystem::path builtElf = cmakeBuildDir / "stride_firmware.elf";
  if (!std::filesystem::exists(builtUf2)) {
    err << "stride: error: expected UF2 not produced: " << builtUf2.string()
        << "\n";
    return false;
  }

  std::filesystem::path uf2Path = outPath;
  if (uf2Path.extension() == ".elf") {
    uf2Path.replace_extension(".uf2");
  }
  std::error_code ec;
  std::filesystem::copy_file(builtUf2, uf2Path,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  if (ec) {
    err << "stride: error: failed to copy UF2 to " << uf2Path.string() << ": "
        << ec.message() << "\n";
    return false;
  }
  if (std::filesystem::exists(builtElf)) {
    std::filesystem::copy_file(builtElf, elfPath,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
  }

  out << "Built firmware: " << uf2Path.string() << "\n";

  if (opts.upload) {
    if (useProbeRs) {
      return uploadFirmwareWithRunner(hw, hw.stringProperty("cpu"), opts,
                                      elfPath, elfPath, out, err);
    }
    if (useSwd) {
      return flashWithOpenOcd(hw, elfPath, opts, strideRoot, out, err);
    }
    if (useAuto) {
      // Prefer OpenOCD SWD when an OpenOCD binary is already available, else picotool.
      if (!findTool("openocd", "OpenOCD", strideRoot).empty() ||
          !findCachedOpenOcd(strideToolsCacheDir() / "openocd").empty()) {
        if (flashWithOpenOcd(hw, elfPath, opts, strideRoot, out, err)) {
          return true;
        }
      }
      if (picotoolExe.empty()) {
        return flashWithOpenOcd(hw, elfPath, opts, strideRoot, out, err);
      }
    }
    return flashPicoWithPicotool(uf2Path, hw, opts, picotoolExe, out, err);
  }
  return true;
}

bool buildAndUploadStm32Firmware(const BoardBuildOptions &opts,
                                 const BoardSpec &spec,
                                 const ResolvedHardware &hw,
                                 const std::string &strideRoot,
                                 const std::filesystem::path &workDir,
                                 const std::filesystem::path &progObj,
                                 const std::filesystem::path &outPath,
                                 const std::filesystem::path &elfPath,
                                 const std::filesystem::path &hexPath,
                                 std::ostream &out, std::ostream &err) {
  auto findArmTool = [&](const std::string &toolName,
                         const std::string &subDir,
                         const std::string &tmTool) -> std::string {
    if (auto p = findTool(toolName, tmTool, strideRoot); !p.empty()) {
      return p;
    }
    std::filesystem::path vsCodeRoot;
    if (!findPicoVsCodeSdk(vsCodeRoot).empty() && !subDir.empty()) {
      if (auto p = findPicoVsCodeTool(vsCodeRoot, subDir, toolName);
          !p.empty()) {
        return p;
      }
    }
    return {};
  };

  const std::string cmakeExe = findArmTool("cmake", "cmake", "CMake");
  if (cmakeExe.empty()) {
    err << "stride: error: cmake not found in PATH or ToolManager\n";
    return false;
  }
  const std::string ninjaExe = findArmTool("ninja", "ninja", "ninja");
  const std::string armGcc =
      findArmTool("arm-none-eabi-gcc", "toolchain", "ArmCrosscompiler");
  const std::string armObjcopy =
      findArmTool("arm-none-eabi-objcopy", "toolchain", "ArmCrosscompiler");
  if (armGcc.empty() || armObjcopy.empty()) {
    err << "stride: error: arm-none-eabi-gcc / arm-none-eabi-objcopy not found "
           "in PATH or ToolManager\n";
    return false;
  }

  const std::filesystem::path runtimeDir =
      findFrameworkRuntime(strideRoot, spec.frameworkName,
                           spec.frameworkVersion)
          .dir;
  if (runtimeDir.empty()) {
    err << "stride: error: framework " << spec.frameworkName
        << " (or one it inherits) has no runtime/CMakeLists.txt\n";
    return false;
  }

  std::filesystem::path rtDir;
  std::filesystem::path stringSrc;
  if (!prepareStringRuntimeSources(workDir, rtDir, stringSrc, err)) {
    return false;
  }

  // Everything about the chip and board comes from its Stride data.
  Stm32Board board;
  std::string boardError;
  if (!resolveStm32Board(hw, board, boardError)) {
    err << "stride: error: " << boardError << "\n";
    return false;
  }
  const std::filesystem::path boardHeaderPath = workDir / "stride_board.h";
  {
    std::ofstream header(boardHeaderPath, std::ios::binary);
    if (!header) {
      err << "stride: error: failed to write " << boardHeaderPath.string()
          << "\n";
      return false;
    }
    header << stm32BoardHeader(hw, board);
  }
  if (opts.verbose) {
    out << "Clocks: SYSCLK " << board.clock.sysclkHz << " Hz, APB1 "
        << board.clock.pclk1Hz << " Hz, APB2 " << board.clock.pclk2Hz
        << " Hz, " << board.clock.flashLatency << " flash wait states\n";
  }

  const std::filesystem::path cmakeBuildDir =
      workDir / (toLower(spec.frameworkName) + "_cmake");
  if (!ninjaExe.empty() &&
      (std::filesystem::exists(cmakeBuildDir / "CMakeCache.txt") ||
       std::filesystem::exists(cmakeBuildDir / "CMakeFiles"))) {
    std::string cacheText;
    if (std::ifstream cacheIn(cmakeBuildDir / "CMakeCache.txt"); cacheIn) {
      cacheText.assign((std::istreambuf_iterator<char>(cacheIn)),
                       std::istreambuf_iterator<char>());
    }
    if (cacheText.find("CMAKE_GENERATOR:INTERNAL=Ninja") == std::string::npos) {
      std::error_code rmEc;
      std::filesystem::remove_all(cmakeBuildDir, rmEc);
    }
  }
  std::vector<std::string> configArgs = {
      "-S", runtimeDir.string(),
      "-B", cmakeBuildDir.string(),
      "-DCMAKE_C_COMPILER=" + std::filesystem::path(armGcc).generic_string(),
      "-DCMAKE_ASM_COMPILER=" + std::filesystem::path(armGcc).generic_string(),
      "-DCMAKE_OBJCOPY=" + std::filesystem::path(armObjcopy).generic_string(),
      "-DSTRIDE_CPU=" + board.cpu,
      "-DSTRIDE_FPU=" + board.fpu,
      "-DSTRIDE_FLASH_KB=" + std::to_string(board.flashBytes / 1024),
      "-DSTRIDE_RAM_KB=" + std::to_string(board.ramBytes / 1024),
      "-DSTRIDE_BOARD_HEADER=" +
          std::filesystem::absolute(boardHeaderPath).generic_string(),
      "-DSTRIDE_PROGRAM_OBJ=" + std::filesystem::absolute(progObj).string(),
      "-DSTRIDE_STRING_RUNTIME=" + std::filesystem::absolute(stringSrc).string(),
      "-DSTRIDE_JIT_INCLUDE_DIR=" +
          std::filesystem::absolute(rtDir).generic_string()};
  if (!ninjaExe.empty()) {
    configArgs.push_back("-G");
    configArgs.push_back("Ninja");
    configArgs.push_back("-DCMAKE_MAKE_PROGRAM=" +
                         std::filesystem::path(ninjaExe).generic_string());
  }

  if (runSubprocess(cmakeExe, configArgs, opts.verbose, out) != 0) {
    err << "stride: error: " << spec.frameworkName << " CMake configure failed\n";
    return false;
  }
  if (runSubprocess(cmakeExe, {"--build", cmakeBuildDir.string(), "--parallel"},
                    opts.verbose, out) != 0) {
    err << "stride: error: " << spec.frameworkName << " build failed\n";
    return false;
  }

  const std::filesystem::path builtElf = cmakeBuildDir / "stride_firmware.elf";
  const std::filesystem::path builtBin = cmakeBuildDir / "stride_firmware.bin";
  const std::filesystem::path builtHex = cmakeBuildDir / "stride_firmware.hex";
  if (!std::filesystem::exists(builtElf)) {
    err << "stride: error: expected ELF not produced: " << builtElf.string()
        << "\n";
    return false;
  }

  std::error_code ec;
  std::filesystem::copy_file(builtElf, elfPath,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  if (std::filesystem::exists(builtHex)) {
    std::filesystem::copy_file(builtHex, hexPath,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
  }
  const std::filesystem::path primarySrc =
      (outPath.extension() == ".hex" && std::filesystem::exists(builtHex))
          ? builtHex
          : ((outPath.extension() == ".bin" && std::filesystem::exists(builtBin))
                 ? builtBin
                 : builtElf);
  if (primarySrc != elfPath && primarySrc != hexPath) {
    std::filesystem::copy_file(
        primarySrc, outPath, std::filesystem::copy_options::overwrite_existing,
        ec);
  }

  out << "Built firmware: " << outPath.string() << "\n";

  if (opts.upload) {
    const std::string method = resolveFlashMethod(hw, opts);
    if (method == "openocd" || method == "auto") {
      return flashWithOpenOcd(hw, elfPath, opts, strideRoot, out, err);
    }
    return uploadFirmwareWithRunner(hw, hw.stringProperty("cpu"), opts, elfPath,
                                    hexPath, out, err);
  }
  return true;
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
  if (!inspectProgramBoard(opts, spec, inspectErr)) {
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

  const FrameworkRuntime frameworkRuntime = findFrameworkRuntime(
      strideRoot, spec.frameworkName, spec.frameworkVersion);
  const bool hasCmakeRuntime = !frameworkRuntime.dir.empty();
  const bool isAvr = (triple == "avr");
  if (!isAvr && !hasCmakeRuntime) {
    err << "stride: error: no supported runtime build recipe found for '"
        << hw->name() << "' (triple '" << triple << "')\n";
    return false;
  }

  std::string defaultExt = ".hex";
  if (auto imgList = hw->property("image");
      imgList && imgList->getNodeType() == AST::List) {
    for (const auto &step : imgList->getChildren()) {
      if (step && step->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(step);
        const std::string kind =
            ASTQuery::valueText(decl->getPropertyValue("kind"));
        if (!kind.empty()) {
          defaultExt = "." + kind;
          break;
        }
      }
    }
  }
  std::filesystem::path inPath(opts.inputFile);
  std::string stem = inPath.stem().string();
  std::filesystem::path outPath =
      opts.outputFile.empty()
          ? (inPath.parent_path().empty()
                 ? std::filesystem::path(stem + defaultExt)
                 : (inPath.parent_path() / (stem + defaultExt)))
          : std::filesystem::path(opts.outputFile);

  std::filesystem::path hexPath = outPath;
  std::filesystem::path elfPath = outPath;
  if (outPath.extension() == ".elf") {
    hexPath.replace_extension(defaultExt);
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

  // 1. Locate toolchain tools (AVR).
  std::string avrGcc;
  std::string avrObjcopy;
  std::string avrSize;
  if (isAvr) {
    avrGcc = findAvrTool("avr-gcc", strideRoot);
    avrObjcopy = findAvrTool("avr-objcopy", strideRoot);
    avrSize = findAvrTool("avr-size", strideRoot);
    if (avrGcc.empty() || avrObjcopy.empty()) {
      const std::string arduinoCli = findInPath("arduino-cli");
      if (!opts.noPrompt && isInteractiveTerminal() && !arduinoCli.empty()) {
        out << "AVR toolchain (avr-gcc / avr-objcopy) not found.\n"
            << "Install Arduino AVR core via 'arduino-cli core install arduino:avr'? [Y/n] "
            << std::flush;
        std::string reply;
        std::getline(std::cin, reply);
        if (reply.empty() || reply[0] == 'y' || reply[0] == 'Y') {
          if (runSubprocess(arduinoCli, {"core", "install", "arduino:avr"},
                            opts.verbose, out) == 0) {
            avrGcc = findAvrTool("avr-gcc", strideRoot);
            avrObjcopy = findAvrTool("avr-objcopy", strideRoot);
            avrSize = findAvrTool("avr-size", strideRoot);
          }
        }
      }
      if (avrGcc.empty() || avrObjcopy.empty()) {
        err << "stride: error: avr-gcc / avr-objcopy not found in PATH or Arduino15 tools "
            << "(install via 'arduino-cli core install arduino:avr' or Arduino IDE)\n";
      }
      return false;
    }
  }

  // 2. Compile Stride source to AVR object file.
  std::filesystem::path progObj = workDir / (stem + ".o");
  if (opts.verbose) {
    out << "Compiling " << opts.inputFile << " for " << hw->name() << " ("
        << triple << ", " << cpu << " @ " << clockHz << " Hz)\n";
  }

  ASTNode compiledTree;
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
    if (!opts.systemOverride.empty()) {
      env.setSystemOverride(opts.systemOverride);
    }
    if (!opts.boardOverride.empty()) {
      env.setBoardOverride(opts.boardOverride);
    }

    if (!env.generateIr(opts.inputFile, false)) {
      err << "stride: error: Stride IR compilation failed\n";
      return false;
    }
    compiledTree = env.mStrideEnv.m_tree;
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
      err << "stride: error: failed to emit object file\n";
      return false;
    }
  }

  if (hasCmakeRuntime) {
    if (frameworkRuntime.builder == "Stm32") {
      return buildAndUploadStm32Firmware(opts, spec, *hw, strideRoot, workDir,
                                         progObj, outPath, elfPath, hexPath,
                                         out, err);
    }
    return buildAndUploadPicoFirmware(opts, spec, *hw, strideRoot, workDir,
                                      progObj, outPath, elfPath, out, err);
  }

  // 3. Generate stride_board.h and compile C runtime.
  std::string boardErr;
  std::string boardHeader = generateBoardHeader(*hw, boardErr, compiledTree);
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
    if (hw->recordString("types", "real") == "Float32") {
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

  // Write normalized target manifest alongside the built firmware.
  {
    TargetDescriptor desc;
    std::string descErr;
    if (inspectTargetDescriptor(opts, desc, descErr)) {
      desc.artifact = elfPath.generic_string();
      desc.hex = hexPath.generic_string();
      std::filesystem::path manifestPath = hexPath;
      manifestPath.replace_extension(".target.json");
      std::ofstream mf(manifestPath, std::ios::binary);
      if (mf) {
        mf << targetDescriptorToJson(desc);
        if (opts.verbose) {
          out << "Target manifest: " << manifestPath.string() << "\n";
        }
      }
    }
  }

  // 5. Optional upload via board's configured flash runner.
  if (opts.upload) {
    if (!uploadFirmwareWithRunner(*hw, cpu, opts, elfPath, hexPath, out, err)) {
      return false;
    }
  }

  return true;
}

void printBoardBuildHelp(std::ostream &os, bool upload) {
  os << "Usage: stride " << (upload ? "flash" : "build")
     << " [options] <file.stride>\n\n"
     << (upload ? "Build board firmware and upload it to a connected board.\n\n"
                : "Build board firmware (.hex and .elf). 'stride flash' also uploads it.\n\n")
     << "Options:\n"
     << "  -o <file.hex|file.elf|file.uf2>\n"
     << "                           Output firmware file (default: <input>.hex or .uf2)\n"
     << "  --system=<name>          Override target platform/system (alias: --platformOverride)\n"
     << "  --boardOverride=<name>   Override target board from 'use <System> on <Board>'\n";
  if (upload) {
    os << "  --port=<port>, -P <port> Serial port for upload (auto-detected if omitted)\n"
       << "  --probe                  Flash using the board's debug probe (e.g. OpenOCD/SWD)\n"
       << "  --upload-method=<name>   Override board upload method (picotool, openocd, swd, probe-rs, auto, ...)\n"
       << "  --probe-interface=<cfg>  Override OpenOCD probe interface config (implies --probe)\n"
       << "  --no-prompt              Do not prompt interactively for BOOTSEL or downloads\n";
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
    std::string uploadMethodOverride;
    std::string probeInterfaceOverride;
    if (arg == "-h" || arg == "--help") {
      printBoardBuildHelp(out, upload);
      return 0;
    } else if (nextVal("-o", opts.outputFile)) {
    } else if (nextVal("--system", opts.systemOverride) ||
               nextVal("--platformOverride", opts.systemOverride)) {
    } else if (nextVal("--boardOverride", opts.boardOverride)) {
    } else if (nextVal("--port", port) || nextVal("-P", port)) {
      if (!upload) {
        err << "stride: error: '" << arg
            << "' only applies to uploading; use 'stride flash'\n";
        return 1;
      }
      opts.serialPort = port;
    } else if (arg == "--probe") {
      if (!upload) {
        err << "stride: error: '--probe' only applies to uploading; use 'stride flash'\n";
        return 1;
      }
      opts.useProbe = true;
    } else if (nextVal("--upload-method", uploadMethodOverride)) {
      if (!upload) {
        err << "stride: error: '" << arg << "' only applies to uploading; use 'stride flash'\n";
        return 1;
      }
      opts.uploadMethod = uploadMethodOverride;
    } else if (nextVal("--probe-interface", probeInterfaceOverride)) {
      if (!upload) {
        err << "stride: error: '" << arg << "' only applies to uploading; use 'stride flash'\n";
        return 1;
      }
      opts.probeInterface = probeInterfaceOverride;
      opts.useProbe = true;
    } else if (arg == "--no-prompt") {
      opts.noPrompt = true;
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

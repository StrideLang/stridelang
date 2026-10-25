#include "stridec/driver.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "stride/parser/ast.h"
#include "stride/stridejit/strideenvironment.hpp"

#include "llvm/Object/Archive.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"

#include <set>
#include <sstream>

namespace strd {

namespace {

bool parseOptionWithVal(const std::string &arg, size_t &i,
                        const std::vector<std::string> &args,
                        const std::string &prefix, std::string &outVal) {
  if (arg == prefix) {
    if (i + 1 < args.size()) {
      outVal = args[++i];
      if (outVal.size() >= 2 && ((outVal.front() == '"' && outVal.back() == '"') ||
                                 (outVal.front() == '\'' && outVal.back() == '\''))) {
        outVal = outVal.substr(1, outVal.size() - 2);
      }
      return true;
    }
  } else if (arg.rfind(prefix + "=", 0) == 0) {
    outVal = arg.substr(prefix.length() + 1);
    if (outVal.size() >= 2 && ((outVal.front() == '"' && outVal.back() == '"') ||
                               (outVal.front() == '\'' && outVal.back() == '\''))) {
      outVal = outVal.substr(1, outVal.size() - 2);
    }
    return true;
  }
  return false;
}

} // namespace

void printHelp(std::ostream &os, const char *progName) {
  os << "Stride Compiler (stridec) - Clang-compatible AOT Driver\n\n"
     << "Usage: " << progName << " [options] <input1.stride> [input2.stride ...]\n\n"
     << "Target & Architecture Selection:\n"
     << "  --target=<triple>, -target <triple>   Set LLVM target triple (e.g. thumbv6m-none-eabi, arm-none-eabi)\n"
     << "  -mcpu=<cpu>, -mcpu <cpu>             Set target CPU (e.g. cortex-m0plus, cortex-m4, cortex-m7)\n"
     << "  -march=<arch>                        Set target architecture\n"
     << "  -mfloat-abi=<abi>                    Floating-point ABI: soft, softfp, hard\n"
     << "  -mfpu=<fpu>                          Target FPU\n\n"
     << "Code Generation & Relocation Options:\n"
     << "  -fPIC, -fno-PIC                      Generate position-independent code\n"
     << "  -fropi, -frwpi                       Read-only / read-write position independence\n"
     << "  -mcmodel=<model>                     Code model: tiny, small, kernel, medium, large\n"
     << "  -Wconversion, --report-conversions   Warn about conversions that can lose data\n"
     << "  -femit-all-functions,                Force code generation for all root-level functions\n"
     << "  --emit-all-functions, --compile-functions\n"
     << "  -O0, -O1, -O2, -O3, -Os, -Oz         Optimization level\n"
     << "  -g, -g2, -g3                         Generate full DWARF debug information\n"
     << "  -gline-tables-only, -g1              Generate line table debug information only\n"
     << "  -g0                                  Disable debug information\n"
     << "  -fvectorize, -fno-vectorize          Enable / disable vectorization\n"
     << "  -funroll-loops, -fno-unroll-loops    Enable / disable loop unrolling\n"
     << "  --build-type=<type>                  Build preset: debug, release, relwithdebinfo\n"
     << "  -c                                   Compile to object file (do not link)\n\n"
     << "Output & Header Options:\n"
     << "  -o <file>                            Output object file (.o / .obj) or static archive (.a)\n"
     << "  --emit-header=<file>, --header-out   Output C interface header file (.h)\n"
     << "  --domain=<name>, -domain <name>      Specific domain to generate header for\n\n"
     << "Arduino Pre-built Library Options:\n"
     << "  --emit-arduino-lib=<dir>, --arduino-lib=<dir>\n"
     << "                                       Output an Arduino-compatible precompiled library bundle\n"
     << "  --arduino-name=<name>                Name of the Arduino library (default: derived from input)\n"
     << "  --arduino-arch=<arch>                Target architecture: arm (default), all\n"
     << "  --arduino-mcu=<mcus>                 Comma-separated list of target MCUs (e.g. cortex-m0plus,cortex-m4)\n"
     << "  --arduino-author=<author>            Author name for library.properties (default: Stride)\n"
     << "  --arduino-version=<ver>              Library version for library.properties (default: 1.0.0)\n\n"
     << "Include & Path Options:\n"
     << "  -I <dir>, -I<dir>                    Add directory to .stride include search path\n"
     << "  -isystem <dir>                       Add system include directory\n"
     << "  --strideroot=<dir>                   Custom Stride root directory\n\n"
     << "Driver Options:\n"
     << "  -v, --verbose                        Enable verbose output\n"
     << "  -h, --help                           Display this help message\n"
     << "  --version                            Display compiler version\n\n"
     << "Examples:\n"
     << "  " << progName << " -c sensor.stride -o sensor.o --emit-header=sensor.h\n"
     << "  " << progName << " -c math.stride filters.stride -o dsp.o --emit-header=dsp.h --emit-all-functions\n"
     << "  " << progName << " -c dsp.stride --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -o dsp.o --emit-header=dsp.h\n"
     << "  " << progName << " --emit-arduino-lib=SensorDSP sensor_dsp.stride\n"
     << "  " << progName << " --emit-arduino-lib=CustomDSP --arduino-mcu=cortex-m4 sensor_dsp.stride\n";
}

void printVersion(std::ostream &os) {
  os << "stridec version 1.0.0 (compatible with LLVM/Clang driver interface)\n";
}

bool parseCommandLine(const std::vector<std::string> &args, StridecOptions &opts,
                      std::string &errorMessage) {
  if (args.empty()) {
    opts.showHelp = true;
    return true;
  }

  for (size_t i = 0; i < args.size(); ++i) {
    const std::string &arg = args[i];
    if (arg.empty()) {
      continue;
    }

    if (arg == "-h" || arg == "--help" || arg == "-help") {
      opts.showHelp = true;
      return true;
    } else if (arg == "--version" || arg == "-version" || (arg == "-v" && args.size() == 1)) {
      opts.showVersion = true;
      return true;
    } else if (arg == "-v" || arg == "--verbose") {
      opts.verbose = true;
    } else if (arg == "-c") {
      // Standard clang compile flag
      continue;
    } else if (arg == "-g" || arg == "-g2" || arg == "-g3") {
      opts.debugInfo = true;
      opts.debugInfoLevel = "full";
    } else if (arg == "-gline-tables-only" || arg == "-g1") {
      opts.debugInfo = true;
      opts.debugInfoLevel = "line-tables-only";
    } else if (arg == "-g0") {
      opts.debugInfo = false;
      opts.debugInfoLevel = "none";
    } else if (arg == "-fvectorize") {
      opts.vectorize = true;
      opts.noVectorize = false;
    } else if (arg == "-fno-vectorize") {
      opts.noVectorize = true;
      opts.vectorize = false;
    } else if (arg == "-funroll-loops") {
      opts.unrollLoops = true;
      opts.noUnrollLoops = false;
    } else if (arg == "-fno-unroll-loops") {
      opts.noUnrollLoops = true;
      opts.unrollLoops = false;
    } else if (arg == "-O0") {
      opts.optimizationLevel = 0;
    } else if (arg == "-O1") {
      opts.optimizationLevel = 1;
    } else if (arg == "-O2" || arg == "-Os" || arg == "-Oz") {
      opts.optimizationLevel = 2;
    } else if (arg == "-O3") {
      opts.optimizationLevel = 3;
    } else if (arg == "-fPIC" || arg == "-fpic" || arg == "-fPIE" || arg == "-fpie") {
      opts.relocModel = "pic";
    } else if (arg == "-fno-PIC" || arg == "-fno-pic" || arg == "-fno-PIE" || arg == "-fno-pie") {
      opts.relocModel = "static";
    } else if (arg == "-fropi") {
      opts.relocModel = "ropi";
    } else if (arg == "-frwpi") {
      opts.relocModel = "rwpi";
    } else if (arg == "-fropi-rwpi") {
      opts.relocModel = "ropi-rwpi";
    } else if (arg == "-Wconversion" || arg == "--report-conversions") {
      opts.reportConversions = true;
    } else if (arg == "-femit-all-functions" || arg == "--emit-all-functions" ||
               arg == "-compile-functions" || arg == "--compile-functions") {
      opts.emitAllFunctions = true;
    } else if (arg.rfind("-O", 0) == 0) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--build-type", opts.buildType) ||
               parseOptionWithVal(arg, i, args, "-build-type", opts.buildType)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "-o", opts.outputFile)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--target", opts.targetTriple) ||
               parseOptionWithVal(arg, i, args, "-target", opts.targetTriple) ||
               parseOptionWithVal(arg, i, args, "--target-triple", opts.targetTriple)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "-mcpu", opts.targetCPU) ||
               parseOptionWithVal(arg, i, args, "--mcpu", opts.targetCPU)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "-march", opts.targetCPU)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "-mcmodel", opts.codeModel) ||
               parseOptionWithVal(arg, i, args, "--code-model", opts.codeModel)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--emit-header", opts.headerFile) ||
               parseOptionWithVal(arg, i, args, "-emit-header", opts.headerFile) ||
               parseOptionWithVal(arg, i, args, "--header-out", opts.headerFile) ||
               parseOptionWithVal(arg, i, args, "-header-out", opts.headerFile)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--domain", opts.domainName) ||
               parseOptionWithVal(arg, i, args, "-domain", opts.domainName)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--emit-arduino-lib", opts.arduinoLibDir) ||
               parseOptionWithVal(arg, i, args, "-emit-arduino-lib", opts.arduinoLibDir) ||
               parseOptionWithVal(arg, i, args, "--arduino-lib", opts.arduinoLibDir) ||
               parseOptionWithVal(arg, i, args, "-arduino-lib", opts.arduinoLibDir)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--arduino-name", opts.arduinoLibName) ||
               parseOptionWithVal(arg, i, args, "-arduino-name", opts.arduinoLibName)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--arduino-arch", opts.arduinoLibArch) ||
               parseOptionWithVal(arg, i, args, "-arduino-arch", opts.arduinoLibArch)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--arduino-mcu", opts.arduinoLibMcu) ||
               parseOptionWithVal(arg, i, args, "-arduino-mcu", opts.arduinoLibMcu)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--arduino-author", opts.arduinoAuthor) ||
               parseOptionWithVal(arg, i, args, "-arduino-author", opts.arduinoAuthor)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--arduino-version", opts.arduinoVersion) ||
               parseOptionWithVal(arg, i, args, "-arduino-version", opts.arduinoVersion)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--target-features", opts.targetFeatures)) {
      continue;
    } else if (parseOptionWithVal(arg, i, args, "--strideroot", opts.strideRoot)) {
      continue;
    } else if (arg.rfind("-I", 0) == 0) {
      if (arg.length() > 2) {
        opts.includeDirs.push_back(arg.substr(2));
      } else if (i + 1 < args.size()) {
        opts.includeDirs.push_back(args[++i]);
      } else {
        errorMessage = "error: -I requires an argument";
        return false;
      }
    } else if (arg == "-isystem") {
      if (i + 1 < args.size()) {
        opts.includeDirs.push_back(args[++i]);
      } else {
        errorMessage = "error: -isystem requires an argument";
        return false;
      }
    } else if (!arg.empty() && arg[0] == '-') {
      errorMessage = "unknown command-line option '" + arg + "'";
      return false;
    } else {
      opts.inputFiles.push_back(arg);
    }
  }

  return true;
}

std::vector<ArduinoTarget> getSupportedArduinoTargets(const std::string &arch) {
  std::vector<ArduinoTarget> targets;
  if (arch == "arm" || arch == "*" || arch == "all") {
    targets.push_back({"cortex-m0plus", "thumbv6m-none-eabi", "cortex-m0plus", "", "cortex-m0plus"});
    targets.push_back({"cortex-m3", "thumbv7m-none-eabi", "cortex-m3", "", "cortex-m3"});
    targets.push_back({"cortex-m4", "thumbv7em-none-eabi", "cortex-m4", "", "cortex-m4"});
    targets.push_back({"cortex-m7", "thumbv7em-none-eabihf", "cortex-m7", "+vfp4", "cortex-m7"});
    targets.push_back({"cortex-m33", "thumbv8m.main-none-eabi", "cortex-m33", "", "cortex-m33"});
  }
  return targets;
}

bool writeStaticArchive(const std::string &archivePath,
                        const std::vector<std::string> &memberPaths,
                        std::string &errorMsg) {
  std::vector<llvm::NewArchiveMember> members;
  for (const auto &path : memberPaths) {
    llvm::Expected<llvm::NewArchiveMember> memberOrErr =
        llvm::NewArchiveMember::getFile(path, /*Deterministic=*/true);
    if (!memberOrErr) {
      errorMsg = llvm::toString(memberOrErr.takeError());
      return false;
    }
    memberOrErr->MemberName = llvm::sys::path::filename(memberOrErr->MemberName);
    members.push_back(std::move(*memberOrErr));
  }

  std::filesystem::path parentDir = std::filesystem::path(archivePath).parent_path();
  if (!parentDir.empty()) {
    std::filesystem::create_directories(parentDir);
  }

  llvm::Error err = llvm::writeArchive(
      archivePath,
      members,
      llvm::SymtabWritingMode::NormalSymtab,
      llvm::object::Archive::K_GNU,
      /*Deterministic=*/true,
      /*Thin=*/false);

  if (err) {
    errorMsg = llvm::toString(std::move(err));
    return false;
  }
  return true;
}

bool emitArduinoLibrary(const StridecOptions &opts, std::ostream &out,
                        std::ostream &err) {
  if (opts.inputFiles.empty()) {
    err << "stridec: error: no input files\n";
    return false;
  }

  for (const auto &inputFile : opts.inputFiles) {
    if (!std::filesystem::exists(inputFile)) {
      err << "stridec: error: no such file or directory: '" << inputFile << "'\n";
      return false;
    }
  }

  std::string libName = opts.arduinoLibName;
  if (libName.empty()) {
    if (!opts.domainName.empty()) {
      libName = opts.domainName;
    } else {
      libName = std::filesystem::path(opts.inputFiles[0]).stem().string();
    }
  }

  std::vector<ArduinoTarget> available = getSupportedArduinoTargets(opts.arduinoLibArch);
  std::vector<ArduinoTarget> selected;
  if (!opts.arduinoLibMcu.empty()) {
    std::set<std::string> requestedMcus;
    std::stringstream ss(opts.arduinoLibMcu);
    std::string item;
    while (std::getline(ss, item, ',')) {
      if (!item.empty()) requestedMcus.insert(item);
    }
    for (const auto &t : available) {
      if (requestedMcus.count(t.mcu)) {
        selected.push_back(t);
      }
    }
    if (selected.empty()) {
      err << "stridec: error: no matching MCU targets found for '" << opts.arduinoLibMcu << "'\n";
      return false;
    }
  } else {
    selected = available;
  }

  std::filesystem::path root(opts.arduinoLibDir);
  std::filesystem::path srcDir = root / "src";
  std::filesystem::path examplesDir = root / "examples" / (libName + "Demo");

  std::error_code ec;
  std::filesystem::create_directories(srcDir, ec);
  std::filesystem::create_directories(examplesDir, ec);

  // 1. Emit C Header
  std::filesystem::path headerPath = srcDir / (libName + ".h");
  if (opts.verbose) {
    out << "Generating Arduino library header: " << headerPath.string() << "\n";
  }
  {
    strd::StrideEnvironment headerEnv(opts.strideRoot);
    for (const auto &inc : opts.includeDirs) {
      headerEnv.addIncludePath(inc);
    }
    if (!headerEnv.generateIr(opts.inputFiles, opts.emitAllFunctions)) {
      err << "stridec: error: failed to generate IR for C header\n";
      return false;
    }
    if (!headerEnv.emitCHeader(headerPath.string(), opts.domainName)) {
      err << "stridec: error: failed to write header file: " << headerPath.string() << "\n";
      return false;
    }
  }

  // 2. Emit C++ compatibility stub
  std::filesystem::path stubPath = srcDir / (libName + ".cpp");
  {
    std::ofstream stubFile(stubPath);
    if (!stubFile) {
      err << "stridec: error: failed to write stub file: " << stubPath.string() << "\n";
      return false;
    }
    stubFile << "// Stride precompiled library stub for Arduino\n"
             << "// Compatibility stub for tools requiring at least one source file in src/\n"
             << "#include \"" << libName << ".h\"\n";
  }

  // 3. Emit library.properties
  std::filesystem::path propsPath = root / "library.properties";
  {
    std::ofstream propsFile(propsPath);
    if (!propsFile) {
      err << "stridec: error: failed to write library.properties: " << propsPath.string() << "\n";
      return false;
    }
    propsFile << "name=" << libName << "\n"
              << "version=" << opts.arduinoVersion << "\n"
              << "author=" << opts.arduinoAuthor << "\n"
              << "maintainer=" << opts.arduinoAuthor << " <info@stridelang.org>\n"
              << "sentence=Precompiled Stride library for Arduino\n"
              << "paragraph=Precompiled Stride digital signal processing and logic library targeting ARM Cortex microcontrollers.\n"
              << "category=Data Processing\n"
              << "url=https://stridelang.org\n"
              << "architectures=" << opts.arduinoLibArch << "\n"
              << "dot_a_linkage=true\n"
              << "precompiled=full\n"
              << "ldflags=-l" << libName << " -lm\n"
              << "includes=" << libName << ".h\n";
  }

  // 4. Emit Example Demo Sketch
  std::filesystem::path demoPath = examplesDir / (libName + "Demo.ino");
  {
    std::ofstream demoFile(demoPath);
    if (demoFile) {
      demoFile << "#include <" << libName << ".h>\n\n"
               << "void setup() {\n"
               << "  Serial.begin(115200);\n"
               << "  while (!Serial && millis() < 2000);\n"
               << "  Serial.println(\"" << libName << " Arduino library initialized.\");\n"
               << "}\n\n"
               << "void loop() {\n"
               << "  delay(1000);\n"
               << "}\n";
    }
  }

  // 5. Build static archives for each selected target MCU
  for (const auto &target : selected) {
    if (opts.verbose) {
      out << "Compiling pre-built library for MCU " << target.mcu << " (" << target.triple << ")\n";
    }
    std::filesystem::path targetDir = srcDir / target.subfolder;
    std::filesystem::create_directories(targetDir, ec);

    std::filesystem::path tempObj = targetDir / (libName + ".o");
    std::filesystem::path archivePath = targetDir / ("lib" + libName + ".a");

    strd::StrideEnvironment env(opts.strideRoot);
    for (const auto &inc : opts.includeDirs) {
      env.addIncludePath(inc);
    }
    if (!env.generateIr(opts.inputFiles, opts.emitAllFunctions)) {
      err << "stridec: error: IR compilation failed for target " << target.mcu << "\n";
      return false;
    }

    std::string features = !opts.targetFeatures.empty() ? opts.targetFeatures : target.features;
    if (!env.emitObjectFile(tempObj.string(), target.triple, target.cpu, features, "static", opts.codeModel)) {
      err << "stridec: error: failed to emit object file for target " << target.mcu << "\n";
      return false;
    }

    std::string arcErr;
    if (!writeStaticArchive(archivePath.string(), {tempObj.string()}, arcErr)) {
      err << "stridec: error: failed to create static archive " << archivePath.string()
          << ": " << arcErr << "\n";
      std::filesystem::remove(tempObj, ec);
      return false;
    }
    std::filesystem::remove(tempObj, ec);
  }

  if (opts.verbose) {
    out << "Successfully generated Arduino pre-built library in: " << root.string() << "\n";
  }
  return true;
}

int runStridec(const std::vector<std::string> &args, std::ostream &out,
               std::ostream &err) {
  StridecOptions opts;
  std::string errorMessage;

  if (!parseCommandLine(args, opts, errorMessage)) {
    err << "stridec: " << errorMessage << "\n";
    printHelp(err, "stridec");
    return 1;
  }

  if (opts.showHelp) {
    printHelp(out, "stridec");
    return 0;
  }

  if (opts.showVersion) {
    printVersion(out);
    return 0;
  }

  if (opts.inputFiles.empty()) {
    err << "stridec: error: no input files\n";
    return 1;
  }

  if (opts.outputFile.empty() && opts.headerFile.empty() && opts.arduinoLibDir.empty()) {
    err << "stridec: error: no output specified. Use -o <file.o>, --emit-header=<file.h>, or --emit-arduino-lib=<dir>\n";
    return 1;
  }

  // Handle Arduino precompiled library output if requested
  if (!opts.arduinoLibDir.empty()) {
    if (!emitArduinoLibrary(opts, out, err)) {
      return 1;
    }
    if (opts.outputFile.empty() && opts.headerFile.empty()) {
      return 0;
    }
  }

  for (const auto &inputFile : opts.inputFiles) {
    if (!std::filesystem::exists(inputFile)) {
      err << "stridec: error: no such file or directory: '" << inputFile << "'\n";
      return 1;
    }
  }

  strd::StrideEnvironment env(opts.strideRoot);
  for (const auto &inc : opts.includeDirs) {
    env.addIncludePath(inc);
  }

  if (!opts.buildType.empty()) {
    std::string bt = opts.buildType;
    for (auto &c : bt) {
      c = static_cast<char>(std::tolower(c));
    }
    if (bt == "debug") {
      env.setBuildType(strd::BuildType::Debug);
    } else if (bt == "release") {
      env.setBuildType(strd::BuildType::Release);
    } else if (bt == "relwithdebinfo") {
      env.setBuildType(strd::BuildType::RelWithDebInfo);
    } else if (bt == "minsizerel") {
      env.setBuildType(strd::BuildType::MinSizeRel);
    }
  }

  if (opts.debugInfo) {
    env.enableDebugInfo(true);
    if (opts.debugInfoLevel == "line-tables-only") {
      env.setDebugInfoLevel(strd::DebugInfoLevel::LineTablesOnly);
    } else {
      env.setDebugInfoLevel(strd::DebugInfoLevel::Full);
    }
  }

  if (opts.optimizationLevel >= 0) {
    env.setOptimizationLevel(opts.optimizationLevel);
  }

  if (opts.verbose) {
    out << "Compiling " << opts.inputFiles.size() << " Stride file(s):\n";
    for (const auto &inputFile : opts.inputFiles) {
      out << "  " << inputFile << "\n";
    }
    if (!opts.targetTriple.empty()) {
      out << "Target Triple: " << opts.targetTriple << "\n";
    }
    if (!opts.targetCPU.empty()) {
      out << "Target CPU: " << opts.targetCPU << "\n";
    }
    if (opts.emitAllFunctions) {
      out << "Emit All Functions: enabled\n";
    }
  }

  bool ok = env.generateIr(opts.inputFiles, opts.emitAllFunctions);
  if (!ok) {
    err << "stridec: error: compilation failed\n";
    return 1;
  }

  if (opts.reportConversions) {
    // file:line:column: warning: ... is the format editors parse
    for (const auto &warning : env.getConversionWarnings()) {
      err << warning.filename << ":" << warning.lineNumber;
      if (warning.columnNumber > 0) {
        err << ":" << warning.columnNumber;
      }
      err << ": warning: conversion from '" << warning.errorTokens[0]
          << "' to '" << warning.errorTokens[1] << "' "
          << warning.errorTokens[2] << " [-Wconversion]\n";
    }
  }

  // 1. Emit C Header if requested
  if (!opts.headerFile.empty()) {
    if (opts.verbose) {
      out << "Generating C header: " << opts.headerFile << "\n";
    }
    if (!env.emitCHeader(opts.headerFile, opts.domainName)) {
      err << "stridec: error: failed to write header file to '" << opts.headerFile << "'\n";
      return 1;
    }
  }

  // 2. Emit Object File or Static Archive if requested
  if (!opts.outputFile.empty()) {
    bool isArchive = false;
    if (opts.outputFile.size() >= 2 && opts.outputFile.substr(opts.outputFile.size() - 2) == ".a") {
      isArchive = true;
    }

    if (opts.verbose) {
      out << (isArchive ? "Emitting static archive: " : "Emitting object file: ") << opts.outputFile << "\n";
    }

    std::string effectiveCPU = opts.targetCPU;
    if (effectiveCPU == "x86_64") {
      effectiveCPU = "x86-64";
    }

    std::error_code ec;
    std::string effectiveObj = isArchive ? (opts.outputFile + ".tmp.o") : opts.outputFile;
    if (!env.emitObjectFile(effectiveObj, opts.targetTriple, effectiveCPU,
                            opts.targetFeatures, opts.relocModel, opts.codeModel)) {
      err << "stridec: error: failed to write object file to '" << effectiveObj << "'\n";
      return 1;
    }

    if (isArchive) {
      std::string arcErr;
      if (!writeStaticArchive(opts.outputFile, {effectiveObj}, arcErr)) {
        err << "stridec: error: failed to create static archive: " << arcErr << "\n";
        std::filesystem::remove(effectiveObj, ec);
        return 1;
      }
      std::filesystem::remove(effectiveObj, ec);
    }
  }

  if (opts.verbose) {
    out << "Stride compilation completed successfully.\n";
  }

  return 0;
}

} // namespace strd

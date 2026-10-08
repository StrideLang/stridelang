#include "stridec/driver.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "stride/parser/ast.h"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/utils/logger.h"

namespace strd {

namespace {

bool parseOptionWithVal(const std::string &arg, size_t &i,
                        const std::vector<std::string> &args,
                        const std::string &prefix, std::string &outVal) {
  if (arg == prefix) {
    if (i + 1 < args.size()) {
      outVal = args[++i];
      return true;
    }
  } else if (arg.rfind(prefix + "=", 0) == 0) {
    outVal = arg.substr(prefix.length() + 1);
    return true;
  }
  return false;
}

} // namespace

void printHelp(std::ostream &os, const char *progName) {
  os << "Stride Compiler (stridec) - Clang-compatible AOT Driver\n\n"
     << "Usage: " << progName << " [options] <input.stride>\n\n"
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
     << "  -O0, -O1, -O2, -O3, -Os, -Oz         Optimization level\n"
     << "  -c                                   Compile to object file (do not link)\n\n"
     << "Output & Header Options:\n"
     << "  -o <file>                            Output object file (.o)\n"
     << "  --emit-header=<file>, --header-out   Output C interface header file (.h)\n"
     << "  --domain=<name>, -domain <name>      Specific domain to generate header for\n\n"
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
     << "  " << progName << " -c dsp.stride --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -o dsp.o --emit-header=dsp.h\n";
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
    } else if (arg.rfind("-O", 0) == 0) {
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
      if (opts.inputFile.empty()) {
        opts.inputFile = arg;
      } else {
        errorMessage = "error: multiple input files specified: '" + opts.inputFile +
                       "' and '" + arg + "'";
        return false;
      }
    }
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

  if (opts.inputFile.empty()) {
    err << "stridec: error: no input files\n";
    return 1;
  }

  if (opts.outputFile.empty() && opts.headerFile.empty()) {
    err << "stridec: error: no output specified. Use -o <file.o> and/or --emit-header=<file.h>\n";
    return 1;
  }

  if (!std::filesystem::exists(opts.inputFile)) {
    err << "stridec: error: no such file or directory: '" << opts.inputFile << "'\n";
    return 1;
  }

  strd::StrideEnvironment env(opts.strideRoot);
  for (const auto &inc : opts.includeDirs) {
    env.addIncludePath(inc);
  }

  if (opts.verbose) {
    out << "Compiling: " << opts.inputFile << "\n";
    if (!opts.targetTriple.empty()) {
      out << "Target Triple: " << opts.targetTriple << "\n";
    }
    if (!opts.targetCPU.empty()) {
      out << "Target CPU: " << opts.targetCPU << "\n";
    }
  }

  bool ok = env.generateIr(opts.inputFile);
  if (!ok) {
    err << "stridec: error: compilation failed for '" << opts.inputFile << "'\n";
    return 1;
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

  // 2. Emit Object File if requested
  if (!opts.outputFile.empty()) {
    if (opts.verbose) {
      out << "Emitting object file: " << opts.outputFile << "\n";
    }
    if (!env.emitObjectFile(opts.outputFile, opts.targetTriple, opts.targetCPU,
                            opts.targetFeatures, opts.relocModel, opts.codeModel)) {
      err << "stridec: error: failed to write object file to '" << opts.outputFile << "'\n";
      return 1;
    }
  }

  if (opts.verbose) {
    out << "Stride compilation completed successfully.\n";
  }

  return 0;
}

} // namespace strd

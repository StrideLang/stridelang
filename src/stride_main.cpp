/*
    Stride is licensed under the terms of the 3-clause BSD license.

    Copyright (C) 2017-2026. The Regents of the University of California.
    All rights reserved.

    Unified Stride CLI Toolchain Front-end ('stride').
*/

#include "stridec/driver.hpp"
#include "stride/stridegen/stridesystem.hpp"
#include "stride/stridegen/toolmanager.hpp"
#include "stride/utils/astfunctions.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

namespace {

void printUsage(const std::string &prog) {
  std::cout
      << "Stride Toolchain Manager & Driver ('stride')\n\n"
      << "Usage:\n"
      << "  " << prog << " <command> [arguments...]\n"
      << "  " << prog << " <file.stride> [compiler options...] (direct compilation)\n\n"
      << "Commands:\n"
      << "  compile [options] <files...>  Ahead-Of-Time compile Stride sources to object code\n"
      << "  system list                   List systems and platforms available in STRIDEROOT\n"
      << "  system upgrade [dir] [ver]    Upgrade 'use <System> version' in files within a folder\n"
      << "  tool list                     List local hardware and tool templates\n"
      << "  tool generate                 Generate all local hardware/tool configurations\n"
      << "  doctor                        Verify environment, STRIDEROOT, and dependencies\n"
      << "  setup vscode                  Install or update the bundled Stride VS Code extension\n\n"
      << "Options:\n"
      << "  -h, --help                    Show this help message\n"
      << "  -v, --version                 Display version and active STRIDEROOT\n\n"
      << "Run '" << prog << " compile --help' for compiler-specific options.\n";
}

int handleSystemCommand(const std::vector<std::string> &args, const std::string &strideRoot) {
  if (args.empty() || args[0] == "list") {
    std::cout << "Available Systems in STRIDEROOT (" << strideRoot << "):\n";
    std::filesystem::path sysPath = std::filesystem::path(strideRoot) / "systems";
    if (!std::filesystem::exists(sysPath) || !std::filesystem::is_directory(sysPath)) {
      std::cerr << "Error: No 'systems' directory found in STRIDEROOT: " << strideRoot << "\n";
      return 1;
    }
    for (const auto &sysEntry : std::filesystem::directory_iterator{sysPath}) {
      if (sysEntry.is_directory()) {
        std::string sysName = sysEntry.path().filename().generic_string();
        std::vector<std::string> versions;
        for (const auto &verEntry : std::filesystem::directory_iterator{sysEntry.path()}) {
          if (verEntry.is_directory()) {
            versions.push_back(verEntry.path().filename().generic_string());
          }
        }
        std::cout << "  - " << sysName;
        if (!versions.empty()) {
          std::cout << " [versions: ";
          for (size_t i = 0; i < versions.size(); ++i) {
            std::cout << versions[i] << (i + 1 < versions.size() ? ", " : "");
          }
          std::cout << "]";
        }
        std::cout << "\n";
      }
    }
    return 0;
  }

  if (args[0] == "upgrade") {
    std::filesystem::path targetDir = std::filesystem::current_path();
    std::string targetVersion = "1.0";

    if (args.size() >= 2) {
      if (std::filesystem::is_directory(args[1])) {
        targetDir = args[1];
        if (args.size() >= 3) {
          targetVersion = args[2];
        }
      } else {
        targetVersion = args[1];
      }
    }

    std::cout << "Scanning folder '" << targetDir.string() << "' for Stride files to upgrade to version "
              << targetVersion << "...\n";

    std::regex useRegex(R"((\buse\s+[\w:]+\s+version\s+)([0-9]+\.[0-9]+))");
    int filesModified = 0;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(targetDir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".stride") {
        std::ifstream inFile(entry.path());
        if (!inFile) continue;
        std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
        inFile.close();

        std::string updated = std::regex_replace(content, useRegex, "$1" + targetVersion);
        if (updated != content) {
          std::ofstream outFile(entry.path(), std::ios::trunc);
          if (outFile) {
            outFile << updated;
            std::cout << "  Upgraded: " << entry.path().filename().string() << "\n";
            filesModified++;
          }
        }
      }
    }

    std::cout << "Upgrade completed. Modified " << filesModified << " file(s).\n";
    return 0;
  }

  std::cerr << "Unknown system subcommand: " << args[0] << "\n";
  return 1;
}

int handleToolCommand(const std::vector<std::string> &args, const std::string &strideRoot) {
  strd::ToolManager toolManager(strideRoot);

  if (args.empty() || args[0] == "list") {
    std::cout << "Stride Hardware & Tool Templates (" << strideRoot << "):\n";
    if (toolManager.toolTemplates.empty()) {
      std::cout << "  (No tool templates discovered in STRIDEROOT/tools)\n";
    }
    for (const auto &toolTemplate : toolManager.toolTemplates) {
      std::cout << "  - " << toolTemplate->strideName << " (" << toolTemplate->displayName << ")\n";
    }
    return 0;
  }

  if (args[0] == "generate") {
    std::cout << "Generating all local hardware and tool configurations...\n";
    toolManager.updateAllLocalConfigs();
    std::cout << "Local tool configuration updated successfully.\n";
    return 0;
  }

  std::cerr << "Unknown tool subcommand: " << args[0] << "\n";
  return 1;
}

int handleDoctorCommand(const std::string &strideRoot) {
  std::cout << "=== Stride Doctor Diagnostic Check ===\n\n";

  // 1. STRIDEROOT check
  std::cout << "[1/4] Checking STRIDEROOT...\n";
  if (strideRoot.empty()) {
    std::cout << "  [FAIL] STRIDEROOT is not set and could not be detected automatically.\n";
  } else {
    std::cout << "  [OK] STRIDEROOT: " << strideRoot << "\n";
    std::filesystem::path libPath = std::filesystem::path(strideRoot) / "library";
    std::filesystem::path sysPath = std::filesystem::path(strideRoot) / "systems";
    if (std::filesystem::exists(libPath)) {
      std::cout << "  [OK] Standard library folder found.\n";
    } else {
      std::cout << "  [WARN] Missing 'library/' directory in STRIDEROOT.\n";
    }
    if (std::filesystem::exists(sysPath)) {
      std::cout << "  [OK] Systems folder found.\n";
    } else {
      std::cout << "  [WARN] Missing 'systems/' directory in STRIDEROOT.\n";
    }
  }

  // 2. PATH check
  std::cout << "\n[2/4] Checking PATH environment...\n";
  const char *pathEnv = std::getenv("PATH");
  if (pathEnv) {
    std::cout << "  [OK] System PATH is accessible.\n";
  } else {
    std::cout << "  [FAIL] PATH environment variable is empty or unavailable.\n";
  }

  // 3. C++ Host Compiler Check
  std::cout << "\n[3/4] Checking Host C++ Compiler / Toolchain...\n";
#if defined(_WIN32)
  int clStatus = std::system("where cl >nul 2>&1");
  int clangStatus = std::system("where clang >nul 2>&1");
  if (clangStatus == 0) {
    std::cout << "  [OK] Clang LLVM compiler detected in PATH.\n";
  } else if (clStatus == 0) {
    std::cout << "  [OK] MSVC cl.exe compiler detected in PATH.\n";
  } else {
    std::cout << "  [WARN] Neither clang nor cl.exe detected in current PATH.\n";
  }
#else
  int clangStatus = std::system("which clang >/dev/null 2>&1");
  int gccStatus = std::system("which gcc >/dev/null 2>&1");
  if (clangStatus == 0) {
    std::cout << "  [OK] Clang detected in PATH.\n";
  } else if (gccStatus == 0) {
    std::cout << "  [OK] GCC detected in PATH.\n";
  } else {
    std::cout << "  [WARN] Neither clang nor gcc detected in PATH.\n";
  }
#endif

  // 4. VS Code Integration Check
  std::cout << "\n[4/4] Checking VS Code CLI...\n";
#if defined(_WIN32)
  int codeStatus = std::system("where code >nul 2>&1");
#else
  int codeStatus = std::system("which code >/dev/null 2>&1");
#endif
  if (codeStatus == 0) {
    std::cout << "  [OK] Visual Studio Code CLI ('code') is installed and in PATH.\n";
  } else {
    std::cout << "  [INFO] VS Code CLI ('code') not found in PATH.\n";
  }

  std::cout << "\nDoctor check completed.\n";
  return 0;
}

int handleSetupVscode(const std::string &strideRoot) {
  std::cout << "Setting up Stride VS Code Extension...\n";

  // Look for bundled VSIX in standard locations:
  std::vector<std::filesystem::path> vsixCandidates;
  if (!strideRoot.empty()) {
    vsixCandidates.push_back(std::filesystem::path(strideRoot).parent_path() / "vscode" / "stride-language.vsix");
    vsixCandidates.push_back(std::filesystem::path(strideRoot) / "vscode" / "stride-language.vsix");
  }
#if defined(_WIN32)
  const char *localApp = std::getenv("LOCALAPPDATA");
  if (localApp) {
    vsixCandidates.push_back(std::filesystem::path(localApp) / "Programs" / "Stride" / "vscode" / "stride-language.vsix");
  }
#endif

  std::filesystem::path foundVsix;
  for (const auto &cand : vsixCandidates) {
    if (std::filesystem::exists(cand)) {
      foundVsix = cand;
      break;
    }
  }

  std::string cmd;
  if (!foundVsix.empty()) {
    std::cout << "Found bundled extension: " << foundVsix.string() << "\n";
    cmd = "code --install-extension \"" + foundVsix.string() + "\" --force";
  } else {
    std::cout << "No local .vsix found. Attempting installation from marketplace...\n";
    cmd = "code --install-extension StrideLang.stride-language";
  }

  std::cout << "Executing: " << cmd << "\n";
  int res = std::system(cmd.c_str());
  if (res == 0) {
    std::cout << "[OK] Stride VS Code extension installed successfully!\n";
    return 0;
  } else {
    std::cerr << "[FAIL] Failed to install VS Code extension (exit code " << res << ").\n"
              << "Please ensure VS Code is installed and 'code' is available in your PATH.\n";
    return 1;
  }
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printUsage(argv[0]);
    return 0;
  }

  std::string firstArg = argv[1];

  if (firstArg == "-h" || firstArg == "--help") {
    printUsage(argv[0]);
    return 0;
  }

  std::string strideRoot = strd::ASTFunctions::getDefaultStrideRoot();

  if (firstArg == "-v" || firstArg == "--version") {
    std::cout << "stride version 1.0.0 (Stride Toolchain & Driver)\n"
              << "STRIDEROOT: " << (strideRoot.empty() ? "(none detected)" : strideRoot) << "\n";
    return 0;
  }

  // Subcommand dispatch
  if (firstArg == "compile") {
    std::vector<std::string> compileArgs;
    for (int i = 2; i < argc; ++i) {
      compileArgs.push_back(argv[i]);
    }
    return strd::runStridec(compileArgs);
  }

  if (firstArg == "system") {
    std::vector<std::string> sysArgs;
    for (int i = 2; i < argc; ++i) {
      sysArgs.push_back(argv[i]);
    }
    return handleSystemCommand(sysArgs, strideRoot);
  }

  if (firstArg == "tool") {
    std::vector<std::string> toolArgs;
    for (int i = 2; i < argc; ++i) {
      toolArgs.push_back(argv[i]);
    }
    return handleToolCommand(toolArgs, strideRoot);
  }

  if (firstArg == "doctor") {
    return handleDoctorCommand(strideRoot);
  }

  if (firstArg == "setup") {
    if (argc >= 3 && std::string(argv[2]) == "vscode") {
      return handleSetupVscode(strideRoot);
    }
    std::cerr << "Unknown setup target. Use 'stride setup vscode'.\n";
    return 1;
  }

  // Direct compilation shorthand: e.g. 'stride test.stride -o test.o' or compiler flags
  if (firstArg.size() >= 7 && firstArg.substr(firstArg.size() - 7) == ".stride") {
    std::vector<std::string> compileArgs;
    for (int i = 1; i < argc; ++i) {
      compileArgs.push_back(argv[i]);
    }
    return strd::runStridec(compileArgs);
  }

  if (!firstArg.empty() && firstArg[0] == '-') {
    std::vector<std::string> compileArgs;
    for (int i = 1; i < argc; ++i) {
      compileArgs.push_back(argv[i]);
    }
    return strd::runStridec(compileArgs);
  }

  std::cerr << "Unknown stride command: '" << firstArg << "'. Run 'stride --help' for available commands.\n";
  return 1;
}

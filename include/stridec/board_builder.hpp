#pragma once

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace strd {

class ResolvedHardware;

struct BoardSpec {
  std::string platformName;
  std::string boardName;
  std::string frameworkName;
  std::string frameworkVersion;
};

struct BoardBuildOptions {
  std::string inputFile;
  std::string outputFile; // .hex or .elf; empty derives from inputFile
  std::string buildDir;   // optional intermediate dir; empty creates beside output or temp
  std::string strideRoot;
  std::string boardOverride;
  std::vector<std::string> includeDirs;
  std::vector<std::string> featureToggles;
  std::vector<std::string> disabledFeatures;
  bool noDefaultFeatures = false;
  std::string serialPort;
  bool upload = false;
  bool verbose = false;
  bool strict = false;
  int optimizationLevel = 2;
};

// Inspects the .stride file's `use <System> version <ver> on <Board>` node.
// Returns true if a hardware board/device target is present on the system node.
bool inspectProgramBoard(const std::string &inputFile,
                         const std::string &strideRoot,
                         const std::string &boardOverride,
                         BoardSpec &outSpec, std::string &errorMsg);

// Generates stride_board.h contents for an AVR (or port-mapped) board.
std::string generateBoardHeader(const ResolvedHardware &hw,
                                std::string &errorMsg);

// Locates an AVR toolchain executable (e.g. "avr-gcc", "avr-objcopy",
// "avr-size", "avrdude") in PATH, ToolManager configs, or Arduino15 tools.
std::string findAvrTool(const std::string &toolName,
                        const std::string &strideRoot);

// Attempts to detect an attached USB serial port matching the board's `usb`
// VID:PID entries. Returns empty string and sets errorMsg if not found.
std::string detectBoardSerialPort(const ResolvedHardware &hw,
                                  std::string &errorMsg);

// End-to-end build (.stride -> .o -> runtime .o -> .elf -> .hex) and optional
// upload via avrdude.
bool buildBoardFirmware(const BoardBuildOptions &opts,
                        std::ostream &out = std::cout,
                        std::ostream &err = std::cerr);

} // namespace strd

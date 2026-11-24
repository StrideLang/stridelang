#pragma once

#include "stride/parser/ast.h"

#include <filesystem>
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
  std::string uploadMethod;   // optional override ("picotool", "openocd", "probe-rs", ...)
  std::string probeInterface; // optional OpenOCD interface override (e.g. "interface/cmsis-dap.cfg")
  bool useProbe = false;      // true when --probe is passed
  bool upload = false;
  bool verbose = false;
  bool strict = false;
  int optimizationLevel = 2;
};

struct SerialDeviceInfo {
  std::string port;
  std::string vidPid; // e.g. "2341:0043" when available
};

struct TargetDescriptor {
  std::string kind;      // "embedded" or "desktop"
  std::string system;    // e.g. "AVR", "JIT"
  std::string board;     // e.g. "ArduinoUno" or "Host"
  std::string framework; // e.g. "AVR", "JIT"
  std::string triple;    // e.g. "avr" or host triple
  std::string cpu;       // e.g. "atmega328p" or "generic"
  std::string artifact;  // .elf or desktop executable/object
  std::string hex;       // .hex (embedded, if applicable)
  std::string flashRunner = "none"; // "avrdude", "arduino-cli", "openocd", "probe-rs", "picotool", "none"
  std::string port;
  int64_t baud = 115200;
  std::string monitorType = "stdio"; // "serial", "rtt", "stdio"
  std::string debugAdapter = "none"; // "cppvsdbg", "cppdbg", "lldb", "cortex-debug", "probe-rs", "avr-gdb", "none"
  std::string chip;
  std::string svd;
};

// Inspects the .stride file's `use <System> version <ver> on <Board>` node.
// Returns true if a hardware board/device target is present on the system node.
bool inspectProgramBoard(const std::string &inputFile,
                         const std::string &strideRoot,
                         const std::string &boardOverride,
                         BoardSpec &outSpec, std::string &errorMsg);

// Generates stride_board.h contents for an AVR (or port-mapped) board.
std::string generateBoardHeader(const ResolvedHardware &hw,
                                std::string &errorMsg,
                                const ASTNode &resolvedTree = nullptr);

// Locates an AVR toolchain executable (e.g. "avr-gcc", "avr-objcopy",
// "avr-size", "avrdude") in PATH, ToolManager configs, or Arduino15 tools.
std::string findAvrTool(const std::string &toolName,
                        const std::string &strideRoot);

// Enumerates active serial ports on the host (with USB VID:PID when available).
std::vector<SerialDeviceInfo> enumerateSerialDevices();

// Attempts to detect an attached USB serial port matching the board's `usb`
// VID:PID entries. Returns empty string and sets errorMsg if not found.
std::string detectBoardSerialPort(const ResolvedHardware &hw,
                                  std::string &errorMsg);

// Resolves a normalized TargetDescriptor for either an embedded board program
// or a desktop/JIT program.
bool inspectTargetDescriptor(const BoardBuildOptions &opts,
                             TargetDescriptor &outDesc, std::string &errorMsg);

// Serializes a TargetDescriptor to JSON.
std::string targetDescriptorToJson(const TargetDescriptor &desc);

// End-to-end build (.stride -> .o -> runtime .o -> .elf -> .hex) and optional
// upload via the board's configured upload runner.
bool buildBoardFirmware(const BoardBuildOptions &opts,
                        std::ostream &out = std::cout,
                        std::ostream &err = std::cerr);

// Dispatches firmware upload to the runner configured in the board's
// `upload.method` block (avrdude, arduino-cli, openocd, probe-rs, picotool, none).
bool uploadFirmwareWithRunner(const ResolvedHardware &hw,
                              const std::string &cpu,
                              const BoardBuildOptions &opts,
                              const std::filesystem::path &elfPath,
                              const std::filesystem::path &hexPath,
                              std::ostream &out = std::cout,
                              std::ostream &err = std::cerr);

// Builds a desktop/JIT target (emits object + C header + .target.json).
bool buildDesktopTarget(const BoardBuildOptions &opts,
                        std::ostream &out = std::cout,
                        std::ostream &err = std::cerr);

void printBoardBuildHelp(std::ostream &os, bool upload);

// The 'stride build' (upload = false) and 'stride flash' (upload = true)
// commands: parses their arguments and runs buildBoardFirmware. This is the
// only command line that builds or uploads firmware.
int runBoardBuild(const std::vector<std::string> &args,
                  const std::string &strideRoot, bool upload,
                  std::ostream &out = std::cout,
                  std::ostream &err = std::cerr);

// Unified 'stride build', 'stride flash', and 'stride run' dispatcher that
// supports both embedded board targets and desktop/JIT targets.
int runUnifiedCommand(const std::vector<std::string> &args,
                      const std::string &strideRoot, const std::string &mode,
                      std::ostream &out = std::cout,
                      std::ostream &err = std::cerr);

// Implements 'stride devices [--json]'.
int runDevicesCommand(const std::vector<std::string> &args,
                      std::ostream &out = std::cout,
                      std::ostream &err = std::cerr);

// Implements 'stride debug-config [--json] <file.stride>'.
int runDebugConfigCommand(const std::vector<std::string> &args,
                          const std::string &strideRoot,
                          std::ostream &out = std::cout,
                          std::ostream &err = std::cerr);

} // namespace strd

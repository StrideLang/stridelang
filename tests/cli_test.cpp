#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "stridec/driver.hpp"
#include "stridec/board_builder.hpp"
#include "stride/stridegen/hardwareresolver.hpp"
#include "stride/stridejit/strideenvironment.hpp"
#include "stride/utils/astfunctions.h"

namespace {

std::string readFileContent(const std::string &path) {
  std::ifstream file(path);
  if (!file)
    return "";
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  file.close();
  return content;
}

bool checkArMagic(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return false;
  char magic[8] = {0};
  f.read(magic, 8);
  f.close();
  return std::string(magic, 8) == "!<arch>\n";
}

} // namespace

class StridecDriverTest : public ::testing::Test {
protected:
  std::stringstream out;
  std::stringstream err;

  void SetUp() override {
    out.str("");
    out.clear();
    err.str("");
    err.clear();
  }
};

TEST_F(StridecDriverTest, HelpFlagsReturnSuccess) {
  // Empty arguments display help
  EXPECT_EQ(strd::runStridec({}, out, err), 0);
  EXPECT_NE(out.str().find("Stride Compiler (stridec)"), std::string::npos);

  out.str("");
  EXPECT_EQ(strd::runStridec({"-h"}, out, err), 0);
  EXPECT_NE(out.str().find("Usage:"), std::string::npos);

  out.str("");
  EXPECT_EQ(strd::runStridec({"--help"}, out, err), 0);
  EXPECT_NE(out.str().find("Usage:"), std::string::npos);

  out.str("");
  EXPECT_EQ(strd::runStridec({"-help"}, out, err), 0);
  EXPECT_NE(out.str().find("Usage:"), std::string::npos);
}

TEST_F(StridecDriverTest, VersionFlagsReturnSuccess) {
  EXPECT_EQ(strd::runStridec({"--version"}, out, err), 0);
  EXPECT_NE(out.str().find("stridec version"), std::string::npos);

  out.str("");
  EXPECT_EQ(strd::runStridec({"-version"}, out, err), 0);
  EXPECT_NE(out.str().find("stridec version"), std::string::npos);

  out.str("");
  EXPECT_EQ(strd::runStridec({"-v"}, out, err), 0);
  EXPECT_NE(out.str().find("stridec version"), std::string::npos);
}

TEST_F(StridecDriverTest, MissingInputFileReturnsError) {
  EXPECT_EQ(strd::runStridec({"-o", "output.o"}, out, err), 1);
  EXPECT_NE(err.str().find("no input files"), std::string::npos);
}

TEST_F(StridecDriverTest, MissingOutputOptionsReturnsError) {
  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  EXPECT_EQ(strd::runStridec({inputFile}, out, err), 1);
  EXPECT_NE(err.str().find("no output specified"), std::string::npos);
}

TEST_F(StridecDriverTest, NonExistentInputFileReturnsError) {
  EXPECT_EQ(
      strd::runStridec({"non_existent_file.stride", "-o", "out.o"}, out, err),
      1);
  EXPECT_NE(err.str().find("no such file or directory"), std::string::npos);
}

TEST_F(StridecDriverTest, UnknownOptionReturnsError) {
  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  EXPECT_EQ(strd::runStridec(
                {"--non-existent-option", inputFile, "-o", "out.o"}, out, err),
            1);
  EXPECT_NE(err.str().find("unknown command-line option"), std::string::npos);
}

// Firmware is built only by 'stride build' and uploaded only by 'stride flash';
// stridec points there instead.
TEST_F(StridecDriverTest, FirmwareOptionsPointToStrideFlash) {
  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  for (const std::vector<std::string> &args :
       std::vector<std::vector<std::string>>{
           {inputFile, "--flash"},
           {inputFile, "--upload"},
           {inputFile, "-o", "out.o", "--port=COM3"},
           {inputFile, "-o", "out.o", "-P", "COM3"}}) {
    out.str("");
    err.str("");
    EXPECT_EQ(strd::runStridec(args, out, err), 1) << args.back();
    EXPECT_NE(err.str().find("use 'stride flash"), std::string::npos)
        << err.str();
  }
}

TEST_F(StridecDriverTest, FirmwareOutputPointsToStrideBuild) {
  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  for (const std::string &output : {"blinky.hex", "blinky.elf"}) {
    out.str("");
    err.str("");
    EXPECT_EQ(strd::runStridec({inputFile, "-o", output}, out, err), 1);
    EXPECT_NE(err.str().find("use 'stride build"), std::string::npos)
        << err.str();
  }
  out.str("");
  err.str("");
  EXPECT_EQ(strd::runStridec({"-c", inputFile, "-o", "blinky.hex"}, out, err), 1);
  EXPECT_NE(err.str().find("use 'stride build"), std::string::npos);
}

TEST_F(StridecDriverTest, BoardBuildHelpListsPortOnlyForFlash) {
  EXPECT_EQ(strd::runBoardBuild({"--help"}, "", true, out, err), 0);
  EXPECT_NE(out.str().find("stride flash"), std::string::npos);
  EXPECT_NE(out.str().find("--port"), std::string::npos);
  EXPECT_NE(out.str().find("--strict"), std::string::npos);
  out.str("");
  EXPECT_EQ(strd::runBoardBuild({"--help"}, "", false, out, err), 0);
  EXPECT_NE(out.str().find("stride build"), std::string::npos);
  EXPECT_EQ(out.str().find("--port"), std::string::npos);
}

TEST_F(StridecDriverTest, BoardBuildRejectsUploadFlagsAndPortOnBuild) {
  EXPECT_EQ(strd::runBoardBuild({"x.stride", "--upload"}, "", false, out, err), 1);
  EXPECT_NE(err.str().find("use 'stride flash'"), std::string::npos);
  err.str("");
  EXPECT_EQ(strd::runBoardBuild({"x.stride", "--flash"}, "", true, out, err), 1);
  EXPECT_NE(err.str().find("use 'stride flash'"), std::string::npos);
  err.str("");
  EXPECT_EQ(strd::runBoardBuild({"x.stride", "--port=COM3"}, "", false, out, err), 1);
  EXPECT_NE(err.str().find("use 'stride flash'"), std::string::npos);
}

TEST_F(StridecDriverTest, BoardBuildRejectsBadArguments) {
  EXPECT_EQ(strd::runBoardBuild({"a.stride", "b.stride"}, "", false, out, err), 1);
  EXPECT_NE(err.str().find("takes one .stride file"), std::string::npos);
  err.str("");
  EXPECT_EQ(strd::runBoardBuild({"a.stride", "-g"}, "", false, out, err), 1);
  EXPECT_NE(err.str().find("unknown option '-g'"), std::string::npos);
  err.str("");
  EXPECT_EQ(strd::runBoardBuild({"a.stride", "-o"}, "", false, out, err), 1);
  EXPECT_NE(err.str().find("requires a value"), std::string::npos);
  err.str("");
  EXPECT_EQ(strd::runBoardBuild({"-v"}, "", true, out, err), 1);
  EXPECT_NE(err.str().find("no input file"), std::string::npos);
}

TEST_F(StridecDriverTest, EmptyArgumentsAreIgnored) {
  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  std::error_code ec;
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path(ec) / "stridec_empty_arg_test";
  std::filesystem::create_directories(tempDir, ec);
  std::string objPath = (tempDir / "sensor.o").string();
  std::string headerPath = (tempDir / "sensor.h").string();

  std::vector<std::string> args = {
      "", "-c", "", inputFile, "", "-o", objPath, "", "--emit-header=" + headerPath, ""};

  int result = strd::runStridec(args, out, err);
  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(objPath));
  EXPECT_TRUE(std::filesystem::exists(headerPath));

  std::filesystem::remove_all(tempDir, ec);
}

TEST_F(StridecDriverTest, CommandLineOptionParserVariations) {
  strd::StridecOptions opts;
  std::string errorMsg;

  std::vector<std::string> args = {"-c",
                                   "--target=thumbv6m-none-eabi",
                                   "-mcpu=cortex-m0plus",
                                   "-mcmodel=small",
                                   "-fPIC",
                                   "-femit-all-functions",
                                   "-I/custom/include",
                                   "-isystem",
                                   "/system/include",
                                   "--domain=SensorDSP",
                                   "--emit-arduino-lib=/custom/arduino/lib",
                                   "--arduino-name=MySensorLib",
                                   "--arduino-arch=arm",
                                   "--arduino-mcu=cortex-m0plus,cortex-m4",
                                   "--arduino-author=StrideTeam",
                                   "--arduino-version=2.1.0",
                                   "-o",
                                   "build/sensor.o",
                                   "--emit-header=build/sensor.h",
                                   "input1.stride",
                                   "input2.stride"};

  ASSERT_TRUE(strd::parseCommandLine(args, opts, errorMsg));
  EXPECT_EQ(opts.targetTriple, "thumbv6m-none-eabi");
  EXPECT_EQ(opts.targetCPU, "cortex-m0plus");
  EXPECT_EQ(opts.codeModel, "small");
  EXPECT_EQ(opts.relocModel, "pic");
  EXPECT_TRUE(opts.emitAllFunctions);
  EXPECT_EQ(opts.domainName, "SensorDSP");
  EXPECT_EQ(opts.arduinoLibDir, "/custom/arduino/lib");
  EXPECT_EQ(opts.arduinoLibName, "MySensorLib");
  EXPECT_EQ(opts.arduinoLibArch, "arm");
  EXPECT_EQ(opts.arduinoLibMcu, "cortex-m0plus,cortex-m4");
  EXPECT_EQ(opts.arduinoAuthor, "StrideTeam");
  EXPECT_EQ(opts.arduinoVersion, "2.1.0");
  EXPECT_EQ(opts.outputFile, "build/sensor.o");
  EXPECT_EQ(opts.headerFile, "build/sensor.h");
  ASSERT_EQ(opts.inputFiles.size(), 2u);
  EXPECT_EQ(opts.inputFiles[0], "input1.stride");
  EXPECT_EQ(opts.inputFiles[1], "input2.stride");
  ASSERT_EQ(opts.includeDirs.size(), 2u);
  EXPECT_EQ(opts.includeDirs[0], "/custom/include");
  EXPECT_EQ(opts.includeDirs[1], "/system/include");
}

TEST_F(StridecDriverTest, EndToEndDriverExecution) {
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "stridec_cli_test";
  std::filesystem::create_directories(tempDir);

  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  std::string objPath = (tempDir / "sensor.o").string();
  std::string headerPath = (tempDir / "sensor.h").string();

  std::vector<std::string> args = {"-c",
                                   inputFile,
                                   "-o",
                                   objPath,
                                   "--emit-header=" + headerPath,
                                   "--target=thumbv6m-none-eabi",
                                   "-mcpu=cortex-m0plus",
                                   "-v"};

  int result = strd::runStridec(args, out, err);
  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(objPath));
  EXPECT_GT(std::filesystem::file_size(objPath), 0);
  EXPECT_TRUE(std::filesystem::exists(headerPath));
  EXPECT_GT(std::filesystem::file_size(headerPath), 0);

  std::filesystem::remove_all(tempDir);
}

TEST_F(StridecDriverTest, MultiFileUnifiedCompilation) {
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "stridec_multi_file_test";
  std::filesystem::create_directories(tempDir);

  std::string file1 = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  std::string file2 = STRIDELANG_TESTS_SOURCE_DIR "math_utils.stride";
  std::string objPath = (tempDir / "unified_output.o").string();
  std::string headerPath = (tempDir / "unified_output.h").string();

  std::vector<std::string> args = {"-c",
                                   file1,
                                   file2,
                                   "-o",
                                   objPath,
                                   "--emit-header=" + headerPath,
                                   "--emit-all-functions",
                                   "-v"};

  int result = strd::runStridec(args, out, err);
  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(objPath));
  EXPECT_GT(std::filesystem::file_size(objPath), 0);
  EXPECT_TRUE(std::filesystem::exists(headerPath));

  std::string headerContent = readFileContent(headerPath);
  EXPECT_NE(headerContent.find("SensorDSP_process"), std::string::npos);
  EXPECT_NE(headerContent.find("Gain"), std::string::npos);
  EXPECT_NE(headerContent.find("Offset"), std::string::npos);

  std::filesystem::remove_all(tempDir);
}

TEST_F(StridecDriverTest, StandaloneFunctionLibraryWithFlag) {
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "stridec_standalone_fn_test";
  std::filesystem::create_directories(tempDir);

  std::string mathFile = STRIDELANG_TESTS_SOURCE_DIR "math_utils.stride";
  std::string objPath = (tempDir / "math_lib.o").string();
  std::string headerPath = (tempDir / "math_lib.h").string();

  std::vector<std::string> args = {"-c",
                                   mathFile,
                                   "-o",
                                   objPath,
                                   "--emit-header=" + headerPath,
                                   "--emit-all-functions",
                                   "-v"};

  int result = strd::runStridec(args, out, err);
  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(objPath));
  EXPECT_GT(std::filesystem::file_size(objPath), 0);
  EXPECT_TRUE(std::filesystem::exists(headerPath));

  std::string headerContent = readFileContent(headerPath);
  EXPECT_NE(headerContent.find("Gain"), std::string::npos);
  EXPECT_NE(headerContent.find("Offset"), std::string::npos);

  std::filesystem::remove_all(tempDir);
}

TEST_F(StridecDriverTest, DirectStaticArchiveOutput) {
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "stridec_direct_archive_test";
  std::error_code ec;
  std::filesystem::remove_all(tempDir, ec);
  std::filesystem::create_directories(tempDir, ec);

  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  std::string archivePath = (tempDir / "libSensorDSP.a").string();

  std::vector<std::string> args = {"-c",
                                   inputFile,
                                   "-o",
                                   archivePath,
                                   "--target=thumbv6m-none-eabi",
                                   "-mcpu=cortex-m0plus",
                                   "-v"};

  int result = strd::runStridec(args, out, err);
  EXPECT_EQ(result, 0);
  EXPECT_TRUE(std::filesystem::exists(archivePath));
  EXPECT_GT(std::filesystem::file_size(archivePath), 0);

  // Validate standard Unix / GNU ar magic "!<arch>\n"
  EXPECT_TRUE(checkArMagic(archivePath));

  std::filesystem::remove_all(tempDir, ec);
}

TEST_F(StridecDriverTest, ArduinoLibraryBundleDefaultArm) {
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "stridec_arduino_arm_test";
  std::error_code ec;
  std::filesystem::remove_all(tempDir, ec);
  std::filesystem::create_directories(tempDir, ec);

  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  std::string libDir = (tempDir / "SensorDSP").string();

  std::vector<std::string> args = {"--emit-arduino-lib=" + libDir,
                                   "--arduino-name=SensorDSP", inputFile, "-v"};

  int result = strd::runStridec(args, out, err);
  EXPECT_EQ(result, 0);

  // 1. Verify library.properties
  std::string propsPath =
      (tempDir / "SensorDSP" / "library.properties").string();
  EXPECT_TRUE(std::filesystem::exists(propsPath));
  std::string propsContent = readFileContent(propsPath);
  EXPECT_NE(propsContent.find("name=SensorDSP"), std::string::npos);
  EXPECT_NE(propsContent.find("architectures=arm"), std::string::npos);
  EXPECT_NE(propsContent.find("precompiled=full"), std::string::npos);
  EXPECT_NE(propsContent.find("dot_a_linkage=true"), std::string::npos);
  EXPECT_NE(propsContent.find("ldflags=-lSensorDSP -lm"), std::string::npos);
  EXPECT_NE(propsContent.find("includes=SensorDSP.h"), std::string::npos);

  // 2. Verify C header & C++ compatibility stub
  std::string headerPath =
      (tempDir / "SensorDSP" / "src" / "SensorDSP.h").string();
  EXPECT_TRUE(std::filesystem::exists(headerPath));
  std::string headerContent = readFileContent(headerPath);
  EXPECT_NE(headerContent.find("SensorDSP_process"), std::string::npos);

  std::string stubPath =
      (tempDir / "SensorDSP" / "src" / "SensorDSP.cpp").string();
  EXPECT_TRUE(std::filesystem::exists(stubPath));
  std::string stubContent = readFileContent(stubPath);
  EXPECT_NE(stubContent.find("#include \"SensorDSP.h\""), std::string::npos);

  // 3. Verify Example Sketch
  std::string demoPath = (tempDir / "SensorDSP" / "examples" / "SensorDSPDemo" /
                          "SensorDSPDemo.ino")
                             .string();
  EXPECT_TRUE(std::filesystem::exists(demoPath));
  std::string demoContent = readFileContent(demoPath);
  EXPECT_NE(demoContent.find("#include <SensorDSP.h>"), std::string::npos);

  // 4. Verify all default ARM Cortex static archives
  std::vector<std::string> expectedMcus = {
      "cortex-m0plus", "cortex-m3", "cortex-m4", "cortex-m7", "cortex-m33"};
  for (const auto &mcu : expectedMcus) {
    std::filesystem::path archFile =
        tempDir / "SensorDSP" / "src" / mcu / "libSensorDSP.a";
    EXPECT_TRUE(std::filesystem::exists(archFile))
        << "Missing expected archive for MCU: " << mcu;
    EXPECT_GT(std::filesystem::file_size(archFile), 0);

    // Validate ar magic
    EXPECT_TRUE(checkArMagic(archFile.string()))
        << "Invalid ar magic for MCU: " << mcu;
  }

  std::filesystem::remove_all(tempDir, ec);
}

TEST_F(StridecDriverTest, ArduinoLibraryBundleTargetedMcuAndCustomName) {
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path() / "stridec_arduino_mcu_test";
  std::error_code ec;
  std::filesystem::remove_all(tempDir, ec);
  std::filesystem::create_directories(tempDir, ec);

  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  std::string libDir = (tempDir / "CustomFilter").string();

  std::vector<std::string> args = {
      "--emit-arduino-lib=" + libDir, "--arduino-name=CustomFilter",
      "--arduino-mcu=cortex-m4",      "--arduino-author=AudioLab",
      "--arduino-version=1.2.3",      inputFile};

  int result = strd::runStridec(args, out, err);
  EXPECT_EQ(result, 0);

  // 1. Verify library.properties
  std::string propsPath =
      (tempDir / "CustomFilter" / "library.properties").string();
  EXPECT_TRUE(std::filesystem::exists(propsPath));
  std::string propsContent = readFileContent(propsPath);
  EXPECT_NE(propsContent.find("name=CustomFilter"), std::string::npos);
  EXPECT_NE(propsContent.find("author=AudioLab"), std::string::npos);
  EXPECT_NE(propsContent.find("version=1.2.3"), std::string::npos);
  EXPECT_NE(propsContent.find("ldflags=-lCustomFilter -lm"), std::string::npos);

  // 2. Verify cortex-m4 archive exists
  std::filesystem::path m4Archive =
      tempDir / "CustomFilter" / "src" / "cortex-m4" / "libCustomFilter.a";
  EXPECT_TRUE(std::filesystem::exists(m4Archive));
  EXPECT_GT(std::filesystem::file_size(m4Archive), 0);

  // 3. Verify other MCUs were NOT generated
  std::filesystem::path m0Archive =
      tempDir / "CustomFilter" / "src" / "cortex-m0plus";
  EXPECT_FALSE(std::filesystem::exists(m0Archive));

  std::filesystem::remove_all(tempDir, ec);
}

TEST_F(StridecDriverTest, DebugAndOptimizationFlags) {
  strd::StridecOptions opts;
  std::string errorMsg;

  // -g flag (full debug info)
  EXPECT_TRUE(strd::parseCommandLine({"-g", "-O0", "--build-type=debug"}, opts,
                                     errorMsg));
  EXPECT_TRUE(opts.debugInfo);
  EXPECT_EQ(opts.debugInfoLevel, "full");
  EXPECT_EQ(opts.optimizationLevel, 0);
  EXPECT_EQ(opts.buildType, "debug");

  // -gline-tables-only
  opts = strd::StridecOptions();
  EXPECT_TRUE(
      strd::parseCommandLine({"-gline-tables-only", "-O2"}, opts, errorMsg));
  EXPECT_TRUE(opts.debugInfo);
  EXPECT_EQ(opts.debugInfoLevel, "line-tables-only");
  EXPECT_EQ(opts.optimizationLevel, 2);

  // -g0 disables debug info
  opts = strd::StridecOptions();
  EXPECT_TRUE(strd::parseCommandLine({"-g0", "-O3"}, opts, errorMsg));
  EXPECT_FALSE(opts.debugInfo);
  EXPECT_EQ(opts.debugInfoLevel, "none");
  EXPECT_EQ(opts.optimizationLevel, 3);

  // Vectorization and loop unrolling flags
  opts = strd::StridecOptions();
  EXPECT_TRUE(strd::parseCommandLine({"-fvectorize", "-funroll-loops"}, opts,
                                     errorMsg));
  EXPECT_TRUE(opts.vectorize);
  EXPECT_FALSE(opts.noVectorize);
  EXPECT_TRUE(opts.unrollLoops);
  EXPECT_FALSE(opts.noUnrollLoops);

  opts = strd::StridecOptions();
  EXPECT_TRUE(strd::parseCommandLine({"-fno-vectorize", "-fno-unroll-loops"},
                                     opts, errorMsg));
  EXPECT_TRUE(opts.noVectorize);
  EXPECT_FALSE(opts.vectorize);
  EXPECT_TRUE(opts.noUnrollLoops);
  EXPECT_FALSE(opts.unrollLoops);
}

// Every example project in strideroot/examples builds into firmware in a
// temporary directory so the examples folder stays clean.
TEST_F(StridecDriverTest, ExamplesBuild) {
  std::string strideRoot = strd::ASTFunctions::getDefaultStrideRoot();
  std::filesystem::path examplesDir =
      std::filesystem::path(strideRoot) / "examples";
  ASSERT_TRUE(std::filesystem::is_directory(examplesDir)) << examplesDir;

  const std::string avrGcc = strd::findAvrTool("avr-gcc", strideRoot);
  const std::string avrObjcopy = strd::findAvrTool("avr-objcopy", strideRoot);
  const bool hasAvr = !avrGcc.empty() && !avrObjcopy.empty();

  auto hasPicoSdk = [&]() -> bool {
    if (const char *env = std::getenv("PICO_SDK_PATH")) {
      if (*env && std::filesystem::exists(env)) {
        return true;
      }
    }
    const char *home = std::getenv("USERPROFILE");
    if (!home) {
      home = std::getenv("HOME");
    }
    if (home && std::filesystem::exists(std::filesystem::path(home) / ".pico-sdk" / "sdk")) {
      return true;
    }
    return false;
  }();

  std::vector<std::filesystem::path> projects;
  for (const auto &entry : std::filesystem::directory_iterator(examplesDir)) {
    if (!entry.is_directory()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.empty() || name[0] == '.') {
      continue;
    }
    if (std::filesystem::exists(entry.path() / (name + ".stride"))) {
      projects.push_back(entry.path());
    } else {
      for (const auto &sub : std::filesystem::directory_iterator(entry.path())) {
        if (!sub.is_directory()) {
          continue;
        }
        const std::string subName = sub.path().filename().string();
        if (std::filesystem::exists(sub.path() / (subName + ".stride"))) {
          projects.push_back(sub.path());
        }
      }
    }
  }
  ASSERT_FALSE(projects.empty()) << examplesDir;

  int built = 0;
  std::error_code ec;
  for (const auto &projectDir : projects) {
    const std::string name = projectDir.filename().string();
    std::filesystem::path program = projectDir / (name + ".stride");
    EXPECT_TRUE(std::filesystem::exists(projectDir / "README.md"))
        << "example " << name << " has no README.md";

    auto build = [&](const std::filesystem::path &input,
                     const std::string &label,
                     const std::vector<std::string> &features,
                     const std::string &ext) {
      std::filesystem::path tempDir =
          std::filesystem::temp_directory_path(ec) / ("stride_example_" + label);
      std::filesystem::remove_all(tempDir, ec);
      std::filesystem::create_directories(tempDir, ec);
      std::ostringstream exampleOut;
      std::ostringstream exampleErr;
      strd::BoardBuildOptions bopts;
      bopts.inputFile = input.string();
      bopts.outputFile = (tempDir / (name + ext)).string();
      bopts.buildDir = (tempDir / "build").string();
      bopts.strideRoot = strideRoot;
      bopts.featureToggles = features;
      bopts.noPrompt = true;
      EXPECT_TRUE(strd::buildBoardFirmware(bopts, exampleOut, exampleErr))
          << label << ": " << exampleErr.str();
      EXPECT_TRUE(std::filesystem::exists(tempDir / (name + ext))) << label;
      std::filesystem::remove_all(tempDir, ec);
      ++built;
    };

    std::ifstream programIn(program);
    std::string source((std::istreambuf_iterator<char>(programIn)),
                       std::istreambuf_iterator<char>());
    programIn.close();

    auto featPos = source.find("feature Board");
    if (featPos != std::string::npos) {
      auto lb = source.find('[', featPos);
      auto rb = source.find(']', lb);
      ASSERT_NE(lb, std::string::npos);
      ASSERT_NE(rb, std::string::npos);
      std::string list = source.substr(lb + 1, rb - lb - 1);
      std::istringstream iss(list);
      std::string item;
      while (std::getline(iss, item, ',')) {
        auto start = item.find_first_not_of(" 	
");
        if (start == std::string::npos) {
          continue;
        }
        auto end = item.find_last_not_of(" 	
");
        std::string variant = item.substr(start, end - start + 1);
        strd::BoardBuildOptions inspectOpts;
        inspectOpts.inputFile = program.string();
        inspectOpts.strideRoot = strideRoot;
        inspectOpts.featureToggles = {"Board::" + variant};
        strd::BoardSpec spec;
        std::string specErr;
        ASSERT_TRUE(strd::inspectProgramBoard(inspectOpts, spec, specErr))
            << name << "_" << variant << ": " << specErr;
        if (spec.platformName == "AVR") {
          if (hasAvr) {
            build(program, name + "_" + variant, {"Board::" + variant}, ".hex");
          }
        } else if (spec.platformName == "Pico") {
          if (hasPicoSdk) {
            build(program, name + "_" + variant, {"Board::" + variant}, ".uf2");
          }
        }
      }
      continue;
    }

    if (!hasAvr) {
      continue;
    }
    build(program, name, {}, ".hex");

    auto usePos = source.find("
use ");
    ASSERT_NE(usePos, std::string::npos) << name << " has no use line";
    auto onPos = source.find(" on ", usePos);
    ASSERT_NE(onPos, std::string::npos) << name << " has no use ... on <Board>";
    auto lineEnd = source.find_first_of("
", onPos);
    const std::string defaultRig = source.substr(onPos + 4, lineEnd - onPos - 4);
    for (const auto &file : std::filesystem::directory_iterator(projectDir)) {
      const std::string stem = file.path().stem().string();
      if (file.path().extension() != ".stride" || stem == name ||
          stem == defaultRig) {
        continue;
      }
      std::ifstream rigIn(file.path());
      std::string rigText((std::istreambuf_iterator<char>(rigIn)),
                          std::istreambuf_iterator<char>());
      if (rigText.find("hardwareBoard " + stem + " ") == std::string::npos) {
        continue;
      }
      std::string variant = source;
      variant.replace(onPos + 4, defaultRig.size(), stem);
      auto importPos = variant.find("import " + defaultRig + "
");
      if (importPos == std::string::npos) {
        importPos = variant.find("import " + defaultRig + "");
      }
      ASSERT_NE(importPos, std::string::npos)
          << name << " does not import " << defaultRig;
      variant.replace(importPos, 7 + defaultRig.size(),
                      "import " + stem + " as " + defaultRig);
      auto variantPath = projectDir / (".variant_" + stem + ".stride");
      {
        std::ofstream variantOut(variantPath);
        variantOut << variant;
      }
      build(variantPath, name + "_" + stem, {}, ".hex");
      std::filesystem::remove(variantPath, ec);
    }
  }
  if (built == 0) {
    GTEST_SKIP() << "no board toolchains installed";
  }
}

// The Mega's ports H to L are above the 8-bit data space: the header says
// so, and its 70 pins number D0-D53 then A0-A15.
TEST_F(StridecDriverTest, ArduinoMegaBoardHeader) {
  std::string strideRoot = strd::ASTFunctions::getDefaultStrideRoot();
  strd::HardwareResolver resolver(strideRoot);
  std::string errMsg;
  auto mega = resolver.resolve("AVR", "ArduinoMega2560", &errMsg);
  ASSERT_TRUE(mega.has_value()) << errMsg;
  std::string header = strd::generateBoardHeader(*mega, errMsg);
  ASSERT_FALSE(header.empty()) << errMsg;
  EXPECT_NE(header.find("#define STRIDE_PIN_COUNT 70"), std::string::npos)
      << header;
  EXPECT_NE(header.find("#define STRIDE_PIN_REG_WIDE 1"), std::string::npos)
      << header;

  auto uno = resolver.resolve("AVR", "ArduinoUno", &errMsg);
  ASSERT_TRUE(uno.has_value()) << errMsg;
  EXPECT_EQ(strd::generateBoardHeader(*uno, errMsg).find("STRIDE_PIN_REG_WIDE"),
            std::string::npos);
}

TEST_F(StridecDriverTest, ArduinoUnoBoardHeaderAndFirmwareBuild) {
  std::string strideRoot = strd::ASTFunctions::getDefaultStrideRoot();
  strd::HardwareResolver resolver(strideRoot);
  std::string errMsg;
  auto uno = resolver.resolve("AVR", "ArduinoUno", &errMsg);
  ASSERT_TRUE(uno.has_value()) << errMsg;

  std::string header = strd::generateBoardHeader(*uno, errMsg);
  ASSERT_FALSE(header.empty()) << errMsg;
  EXPECT_NE(header.find("#define STRIDE_PIN_COUNT 20"), std::string::npos);
  EXPECT_NE(header.find("#define STRIDE_SERIAL_BAUD 115200UL"), std::string::npos);
  EXPECT_NE(header.find("STRIDE_PIN_OUTPUT_REGS"), std::string::npos);

  const std::string avrGcc = strd::findAvrTool("avr-gcc", strideRoot);
  const std::string avrObjcopy = strd::findAvrTool("avr-objcopy", strideRoot);
  if (avrGcc.empty() || avrObjcopy.empty()) {
    GTEST_SKIP() << "avr-gcc / avr-objcopy not installed";
  }

  std::error_code ec;
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path(ec) / "stride_uno_blinky_test";
  std::filesystem::remove_all(tempDir, ec);
  std::filesystem::create_directories(tempDir, ec);

  std::filesystem::path blinkStride = tempDir / "blinky.stride";
  {
    std::ofstream f(blinkStride);
    f << "use AVR version 1.0 on ArduinoUno\n\n"
      << "signal Millis { type: UInt32 domain: AvrTickDomain }\n"
      << "signal Wave { type: Real domain: AvrTickDomain }\n"
      << "switch LedOn { domain: AvrTickDomain }\n\n"
      << "[13, 1] >> __AvrPinMode();\n"
      << "__AvrMillis() >> Millis;\n"
      << "Millis * 0.0062832 >> __Sin() >> Wave;\n"
      << "[Wave, 0.0] >> Greater() >> LedOn;\n"
      << "[13, LedOn] >> __AvrPinWrite();\n";
  }

  std::filesystem::path hexOut = tempDir / "blinky.hex";
  strd::BoardBuildOptions bopts;
  bopts.inputFile = blinkStride.string();
  bopts.outputFile = hexOut.string();
  bopts.buildDir = (tempDir / "build").string();
  bopts.strideRoot = strideRoot;
  EXPECT_TRUE(strd::buildBoardFirmware(bopts, out, err)) << err.str();
  EXPECT_TRUE(std::filesystem::exists(hexOut));
  EXPECT_GT(std::filesystem::file_size(hexOut), 0u);
  EXPECT_TRUE(std::filesystem::exists(tempDir / "blinky.elf"));

  std::filesystem::remove_all(tempDir, ec);
}

TEST_F(StridecDriverTest, ArduinoUnoIsrBoardHeaderAndFirmwareBuild) {
  std::string strideRoot = strd::ASTFunctions::getDefaultStrideRoot();
  strd::HardwareResolver resolver(strideRoot);
  std::string errMsg;
  auto uno = resolver.resolve("AVR", "ArduinoUno", &errMsg);
  ASSERT_TRUE(uno.has_value()) << errMsg;

  std::error_code ec;
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path(ec) / "stride_uno_isr_test";
  std::filesystem::remove_all(tempDir, ec);
  std::filesystem::create_directories(tempDir, ec);

  std::filesystem::path rigFile = tempDir / "IsrRig.stride";
  {
    std::ofstream f(rigFile);
    f << "package IsrRig\n"
      << "import Gpio\n"
      << "import ArduinoUno\n"
      << "hardwareBoard IsrRig { overlays: ArduinoUno }\n"
      << "gpioPin Button { pin: ArduinoUno::D2 mode: Gpio::PinInput pull: Gpio::PullUp interrupt: Gpio::InterruptFalling }\n"
      << "gpioPin FastOutPin { pin: ArduinoUno::D8 mode: Gpio::PinOutput initial: off }\n";
  }

  std::filesystem::path progFile = tempDir / "isr_prog.stride";
  {
    std::ofstream f(progFile);
    f << "use AVR version 1.0 on IsrRig\n"
      << "import DigitalIO\n"
      << "import IsrRig\n"
      << "_domainDefinition PinEdgeIsrDomain {\n"
      << "    framework: _AvrFramework\n"
      << "    parentDomain: RootDomain\n"
      << "    requiresResources: [_RootContext, _AvrProcessor, _AvrFlashMemory, _AvrSram, _AvrGpio]\n"
      << "    execution: ExecutionInterrupt\n"
      << "    trigger: IsrRig::Button\n"
      << "    rate: none\n"
      << "}\n"
      << "switch EdgeState { domain: PinEdgeIsrDomain default: off }\n"
      << "EdgeState >> Not() >> EdgeState;\n"
      << "EdgeState >> DigitalIO::DigitalOut(pin: IsrRig::FastOutPin);\n"
      << "signal TimerTicks { type: Int16 domain: AvrTimer1IsrDomain default: 0 }\n"
      << "TimerTicks + 1 >> TimerTicks;\n";
  }

  strd::StrideEnvironment env(strideRoot);
  env.setTarget("avr", "atmega328p", "");
  ASSERT_TRUE(env.generateIr(progFile.string(), false));

  std::string header =
      strd::generateBoardHeader(*uno, errMsg, env.mStrideEnv.m_tree);
  ASSERT_FALSE(header.empty()) << errMsg;
  EXPECT_NE(header.find("#define STRIDE_ISR_TIMER1_COMPA_VECT AvrTimer1IsrDomain"),
            std::string::npos)
      << header;
  EXPECT_NE(header.find("#define STRIDE_ISR_INT0_VECT PinEdgeIsrDomain"),
            std::string::npos)
      << header;
  EXPECT_NE(header.find("ISR(TIMER1_COMPA_vect)"), std::string::npos) << header;
  EXPECT_NE(header.find("ISR(INT0_vect)"), std::string::npos) << header;

  const std::string avrGcc = strd::findAvrTool("avr-gcc", strideRoot);
  const std::string avrObjcopy = strd::findAvrTool("avr-objcopy", strideRoot);
  if (!avrGcc.empty() && !avrObjcopy.empty()) {
    std::filesystem::path hexOut = tempDir / "isr_prog.hex";
    strd::BoardBuildOptions bopts;
    bopts.inputFile = progFile.string();
    bopts.outputFile = hexOut.string();
    bopts.buildDir = (tempDir / "build").string();
    bopts.strideRoot = strideRoot;
    EXPECT_TRUE(strd::buildBoardFirmware(bopts, out, err)) << err.str();
    EXPECT_TRUE(std::filesystem::exists(hexOut));
  }

  std::filesystem::remove_all(tempDir, ec);
}

TEST_F(StridecDriverTest, DevicesCommandJsonListsHostDesktop) {
  EXPECT_EQ(strd::runDevicesCommand({"--json"}, out, err), 0) << err.str();
  EXPECT_NE(out.str().find("\"kind\": \"desktop\""), std::string::npos);
  EXPECT_NE(out.str().find("\"id\": \"localhost\""), std::string::npos);
}

TEST_F(StridecDriverTest, DebugConfigCommandDesktopAndEmbeddedJson) {
  std::string strideRoot = strd::ASTFunctions::getDefaultStrideRoot();
  std::string desktopFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  EXPECT_EQ(strd::runDebugConfigCommand({"--json", desktopFile}, strideRoot, out, err), 0)
      << err.str();
  EXPECT_NE(out.str().find("\"kind\": \"desktop\""), std::string::npos);
  EXPECT_NE(out.str().find("\"type\": \"stdio\""), std::string::npos);

  std::error_code ec;
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path(ec) / "stride_dbg_cfg_test";
  std::filesystem::remove_all(tempDir, ec);
  std::filesystem::create_directories(tempDir, ec);
  std::filesystem::path unoFile = tempDir / "uno_prog.stride";
  {
    std::ofstream f(unoFile);
    f << "use AVR version 1.0 on ArduinoUno\n"
      << "signal X { type: Real domain: AvrTickDomain }\n";
  }
  out.str("");
  err.str("");
  EXPECT_EQ(strd::runDebugConfigCommand({"--json", unoFile.string()}, strideRoot, out, err), 0)
      << err.str();
  EXPECT_NE(out.str().find("\"kind\": \"embedded\""), std::string::npos);
  EXPECT_NE(out.str().find("\"board\": \"ArduinoUno\""), std::string::npos);
  EXPECT_NE(out.str().find("\"runner\": \"avrdude\""), std::string::npos);
  EXPECT_NE(out.str().find("\"type\": \"serial\""), std::string::npos);
  std::filesystem::remove_all(tempDir, ec);
}

TEST_F(StridecDriverTest, UnifiedBuildAndRunDesktopTarget) {
  std::string strideRoot = strd::ASTFunctions::getDefaultStrideRoot();
  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";

  std::error_code ec;
  std::filesystem::path tempDir =
      std::filesystem::temp_directory_path(ec) / "stride_unified_desktop_test";
  std::filesystem::remove_all(tempDir, ec);
  std::filesystem::create_directories(tempDir, ec);

  std::filesystem::path objOut = tempDir / "sensor_dsp.o";
  EXPECT_EQ(strd::runUnifiedCommand({inputFile, "-o", objOut.string()}, strideRoot, "build", out, err), 0)
      << err.str();
  EXPECT_TRUE(std::filesystem::exists(objOut));
  EXPECT_TRUE(std::filesystem::exists(tempDir / "sensor_dsp.h"));
  EXPECT_TRUE(std::filesystem::exists(tempDir / "sensor_dsp.target.json"));

  out.str("");
  err.str("");
  EXPECT_EQ(strd::runUnifiedCommand({inputFile, "-o", objOut.string()}, strideRoot, "run", out, err), 0)
      << err.str();
  EXPECT_NE(out.str().find("Running desktop JIT target"), std::string::npos);

  std::filesystem::remove_all(tempDir, ec);
}

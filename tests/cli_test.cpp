#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "stridec/driver.hpp"

namespace {

std::string readFileContent(const std::string &path) {
  std::ifstream file(path);
  if (!file) return "";
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  file.close();
  return content;
}

bool checkArMagic(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
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
  EXPECT_EQ(strd::runStridec({"non_existent_file.stride", "-o", "out.o"}, out, err), 1);
  EXPECT_NE(err.str().find("no such file or directory"), std::string::npos);
}

TEST_F(StridecDriverTest, UnknownOptionReturnsError) {
  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  EXPECT_EQ(strd::runStridec({"--non-existent-option", inputFile, "-o", "out.o"}, out, err), 1);
  EXPECT_NE(err.str().find("unknown command-line option"), std::string::npos);
}

TEST_F(StridecDriverTest, CommandLineOptionParserVariations) {
  strd::StridecOptions opts;
  std::string errorMsg;

  std::vector<std::string> args = {
      "-c",
      "--target=thumbv6m-none-eabi",
      "-mcpu=cortex-m0plus",
      "-mcmodel=small",
      "-fPIC",
      "-femit-all-functions",
      "-I/custom/include",
      "-isystem", "/system/include",
      "--domain=SensorDSP",
      "--emit-arduino-lib=/custom/arduino/lib",
      "--arduino-name=MySensorLib",
      "--arduino-arch=arm",
      "--arduino-mcu=cortex-m0plus,cortex-m4",
      "--arduino-author=StrideTeam",
      "--arduino-version=2.1.0",
      "-o", "build/sensor.o",
      "--emit-header=build/sensor.h",
      "input1.stride",
      "input2.stride"
  };

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

  std::vector<std::string> args = {
      "-c",
      inputFile,
      "-o", objPath,
      "--emit-header=" + headerPath,
      "--target=thumbv6m-none-eabi",
      "-mcpu=cortex-m0plus",
      "-v"
  };

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

  std::vector<std::string> args = {
      "-c",
      file1,
      file2,
      "-o", objPath,
      "--emit-header=" + headerPath,
      "--emit-all-functions",
      "-v"
  };

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

  std::vector<std::string> args = {
      "-c",
      mathFile,
      "-o", objPath,
      "--emit-header=" + headerPath,
      "--emit-all-functions",
      "-v"
  };

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

  std::vector<std::string> args = {
      "-c",
      inputFile,
      "-o", archivePath,
      "--target=thumbv6m-none-eabi",
      "-mcpu=cortex-m0plus",
      "-v"
  };

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

  std::vector<std::string> args = {
      "--emit-arduino-lib=" + libDir,
      "--arduino-name=SensorDSP",
      inputFile,
      "-v"
  };

  int result = strd::runStridec(args, out, err);
  EXPECT_EQ(result, 0);

  // 1. Verify library.properties
  std::string propsPath = (tempDir / "SensorDSP" / "library.properties").string();
  EXPECT_TRUE(std::filesystem::exists(propsPath));
  std::string propsContent = readFileContent(propsPath);
  EXPECT_NE(propsContent.find("name=SensorDSP"), std::string::npos);
  EXPECT_NE(propsContent.find("architectures=arm"), std::string::npos);
  EXPECT_NE(propsContent.find("precompiled=full"), std::string::npos);
  EXPECT_NE(propsContent.find("dot_a_linkage=true"), std::string::npos);
  EXPECT_NE(propsContent.find("ldflags=-lSensorDSP -lm"), std::string::npos);
  EXPECT_NE(propsContent.find("includes=SensorDSP.h"), std::string::npos);

  // 2. Verify C header & C++ compatibility stub
  std::string headerPath = (tempDir / "SensorDSP" / "src" / "SensorDSP.h").string();
  EXPECT_TRUE(std::filesystem::exists(headerPath));
  std::string headerContent = readFileContent(headerPath);
  EXPECT_NE(headerContent.find("SensorDSP_process"), std::string::npos);

  std::string stubPath = (tempDir / "SensorDSP" / "src" / "SensorDSP.cpp").string();
  EXPECT_TRUE(std::filesystem::exists(stubPath));
  std::string stubContent = readFileContent(stubPath);
  EXPECT_NE(stubContent.find("#include \"SensorDSP.h\""), std::string::npos);

  // 3. Verify Example Sketch
  std::string demoPath = (tempDir / "SensorDSP" / "examples" / "SensorDSPDemo" / "SensorDSPDemo.ino").string();
  EXPECT_TRUE(std::filesystem::exists(demoPath));
  std::string demoContent = readFileContent(demoPath);
  EXPECT_NE(demoContent.find("#include <SensorDSP.h>"), std::string::npos);

  // 4. Verify all default ARM Cortex static archives
  std::vector<std::string> expectedMcus = {
      "cortex-m0plus", "cortex-m3", "cortex-m4", "cortex-m7", "cortex-m33"
  };
  for (const auto &mcu : expectedMcus) {
    std::filesystem::path archFile = tempDir / "SensorDSP" / "src" / mcu / "libSensorDSP.a";
    EXPECT_TRUE(std::filesystem::exists(archFile)) << "Missing expected archive for MCU: " << mcu;
    EXPECT_GT(std::filesystem::file_size(archFile), 0);

    // Validate ar magic
    EXPECT_TRUE(checkArMagic(archFile.string())) << "Invalid ar magic for MCU: " << mcu;
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
      "--emit-arduino-lib=" + libDir,
      "--arduino-name=CustomFilter",
      "--arduino-mcu=cortex-m4",
      "--arduino-author=AudioLab",
      "--arduino-version=1.2.3",
      inputFile
  };

  int result = strd::runStridec(args, out, err);
  EXPECT_EQ(result, 0);

  // 1. Verify library.properties
  std::string propsPath = (tempDir / "CustomFilter" / "library.properties").string();
  EXPECT_TRUE(std::filesystem::exists(propsPath));
  std::string propsContent = readFileContent(propsPath);
  EXPECT_NE(propsContent.find("name=CustomFilter"), std::string::npos);
  EXPECT_NE(propsContent.find("author=AudioLab"), std::string::npos);
  EXPECT_NE(propsContent.find("version=1.2.3"), std::string::npos);
  EXPECT_NE(propsContent.find("ldflags=-lCustomFilter -lm"), std::string::npos);

  // 2. Verify cortex-m4 archive exists
  std::filesystem::path m4Archive = tempDir / "CustomFilter" / "src" / "cortex-m4" / "libCustomFilter.a";
  EXPECT_TRUE(std::filesystem::exists(m4Archive));
  EXPECT_GT(std::filesystem::file_size(m4Archive), 0);

  // 3. Verify other MCUs were NOT generated
  std::filesystem::path m0Archive = tempDir / "CustomFilter" / "src" / "cortex-m0plus";
  EXPECT_FALSE(std::filesystem::exists(m0Archive));

  std::filesystem::remove_all(tempDir, ec);
}

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
  return std::string((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
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

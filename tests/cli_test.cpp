#include "gtest/gtest.h"
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "stridec/driver.hpp"

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

TEST_F(StridecDriverTest, MultipleInputFilesReturnError) {
  std::string inputFile = STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride";
  EXPECT_EQ(strd::runStridec({inputFile, "another_file.stride", "-o", "out.o"}, out, err), 1);
  EXPECT_NE(err.str().find("multiple input files"), std::string::npos);
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
      "-I/custom/include",
      "-isystem", "/system/include",
      "--domain=SensorDSP",
      "-o", "build/sensor.o",
      "--emit-header=build/sensor.h",
      "input.stride"
  };

  ASSERT_TRUE(strd::parseCommandLine(args, opts, errorMsg));
  EXPECT_EQ(opts.targetTriple, "thumbv6m-none-eabi");
  EXPECT_EQ(opts.targetCPU, "cortex-m0plus");
  EXPECT_EQ(opts.codeModel, "small");
  EXPECT_EQ(opts.relocModel, "pic");
  EXPECT_EQ(opts.domainName, "SensorDSP");
  EXPECT_EQ(opts.outputFile, "build/sensor.o");
  EXPECT_EQ(opts.headerFile, "build/sensor.h");
  EXPECT_EQ(opts.inputFile, "input.stride");
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

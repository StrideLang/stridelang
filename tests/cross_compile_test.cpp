#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "stride/stridejit/strideenvironment.hpp"

#include "llvm/Config/llvm-config.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Triple.h"

namespace {

bool isTargetSupported(const std::string &tripleStr) {
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  std::string error;
  llvm::Triple triple(tripleStr);
#if LLVM_VERSION_MAJOR >= 20
  return llvm::TargetRegistry::lookupTarget(triple, error) != nullptr;
#else
  return llvm::TargetRegistry::lookupTarget(triple.str(), error) != nullptr;
#endif
}

std::vector<uint8_t> readFileBytes(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
}

} // namespace

class CrossCompileTest : public ::testing::Test {
protected:
  strd::StrideEnvironment env;
  std::filesystem::path tempDir;

  void SetUp() override {
    tempDir = std::filesystem::temp_directory_path() / "stride_cross_compile_tests";
    std::filesystem::create_directories(tempDir);
    bool ok = env.generateIr(STRIDELANG_TESTS_SOURCE_DIR "sensor_dsp.stride");
    ASSERT_TRUE(ok);
  }

  void TearDown() override {
    std::filesystem::remove_all(tempDir);
  }
};

TEST_F(CrossCompileTest, TargetRP2040_CortexM0Plus) {
  if (!isTargetSupported("thumbv6m-none-eabi")) {
    GTEST_SKIP() << "Target 'thumbv6m-none-eabi' is not built into the current LLVM library.";
  }

  std::string objFile = (tempDir / "rp2040.o").string();
  EXPECT_TRUE(env.emitObjectFile(objFile, "thumbv6m-none-eabi", "cortex-m0plus"));
  EXPECT_TRUE(std::filesystem::exists(objFile));

  auto bytes = readFileBytes(objFile);
  ASSERT_GE(bytes.size(), 20u);

  // Validate ELF header: \x7f E L F
  EXPECT_EQ(bytes[0], 0x7F);
  EXPECT_EQ(bytes[1], 'E');
  EXPECT_EQ(bytes[2], 'L');
  EXPECT_EQ(bytes[3], 'F');
  // 32-bit ELF & Little Endian
  EXPECT_EQ(bytes[4], 1); // ELFCLASS32
  EXPECT_EQ(bytes[5], 1); // ELFDATA2LSB
  // e_machine: EM_ARM = 0x28 (40)
  uint16_t e_machine = bytes[18] | (bytes[19] << 8);
  EXPECT_EQ(e_machine, 0x28);
}

TEST_F(CrossCompileTest, TargetSTM32_CortexM3) {
  if (!isTargetSupported("thumbv7m-none-eabi")) {
    GTEST_SKIP() << "Target 'thumbv7m-none-eabi' is not built into the current LLVM library.";
  }

  std::string objFile = (tempDir / "stm32_m3.o").string();
  EXPECT_TRUE(env.emitObjectFile(objFile, "thumbv7m-none-eabi", "cortex-m3"));
  EXPECT_TRUE(std::filesystem::exists(objFile));

  auto bytes = readFileBytes(objFile);
  ASSERT_GE(bytes.size(), 20u);
  EXPECT_EQ(bytes[0], 0x7F);
  EXPECT_EQ(bytes[1], 'E');
  EXPECT_EQ(bytes[2], 'L');
  EXPECT_EQ(bytes[3], 'F');
  uint16_t e_machine = bytes[18] | (bytes[19] << 8);
  EXPECT_EQ(e_machine, 0x28);
}

TEST_F(CrossCompileTest, TargetSTM32_CortexM4) {
  if (!isTargetSupported("thumbv7em-none-eabi")) {
    GTEST_SKIP() << "Target 'thumbv7em-none-eabi' is not built into the current LLVM library.";
  }

  std::string objFile = (tempDir / "stm32_m4.o").string();
  EXPECT_TRUE(env.emitObjectFile(objFile, "thumbv7em-none-eabi", "cortex-m4"));
  EXPECT_TRUE(std::filesystem::exists(objFile));

  auto bytes = readFileBytes(objFile);
  ASSERT_GE(bytes.size(), 20u);
  EXPECT_EQ(bytes[0], 0x7F);
  EXPECT_EQ(bytes[1], 'E');
  EXPECT_EQ(bytes[2], 'L');
  EXPECT_EQ(bytes[3], 'F');
  uint16_t e_machine = bytes[18] | (bytes[19] << 8);
  EXPECT_EQ(e_machine, 0x28);
}

TEST_F(CrossCompileTest, TargetSTM32H7_CortexM7_HardFloat) {
  if (!isTargetSupported("thumbv7em-none-eabihf")) {
    GTEST_SKIP() << "Target 'thumbv7em-none-eabihf' is not built into the current LLVM library.";
  }

  std::string objFile = (tempDir / "stm32h7_m7.o").string();
  EXPECT_TRUE(env.emitObjectFile(objFile, "thumbv7em-none-eabihf", "cortex-m7", "+vfp4"));
  EXPECT_TRUE(std::filesystem::exists(objFile));

  auto bytes = readFileBytes(objFile);
  ASSERT_GE(bytes.size(), 20u);
  EXPECT_EQ(bytes[0], 0x7F);
  EXPECT_EQ(bytes[1], 'E');
  EXPECT_EQ(bytes[2], 'L');
  EXPECT_EQ(bytes[3], 'F');
  uint16_t e_machine = bytes[18] | (bytes[19] << 8);
  EXPECT_EQ(e_machine, 0x28);
}

TEST_F(CrossCompileTest, TargetRP2350_CortexM33_ARMv8M) {
  if (!isTargetSupported("thumbv8m.main-none-eabi")) {
    GTEST_SKIP() << "Target 'thumbv8m.main-none-eabi' is not built into the current LLVM library.";
  }

  std::string objFile = (tempDir / "rp2350_m33.o").string();
  EXPECT_TRUE(env.emitObjectFile(objFile, "thumbv8m.main-none-eabi", "cortex-m33"));
  EXPECT_TRUE(std::filesystem::exists(objFile));

  auto bytes = readFileBytes(objFile);
  ASSERT_GE(bytes.size(), 20u);
  EXPECT_EQ(bytes[0], 0x7F);
  EXPECT_EQ(bytes[1], 'E');
  EXPECT_EQ(bytes[2], 'L');
  EXPECT_EQ(bytes[3], 'F');
  uint16_t e_machine = bytes[18] | (bytes[19] << 8);
  EXPECT_EQ(e_machine, 0x28);
}

TEST_F(CrossCompileTest, TargetRISCV32) {
  if (!isTargetSupported("riscv32-unknown-elf")) {
    GTEST_SKIP() << "Target 'riscv32-unknown-elf' is not built into the current LLVM library.";
  }

  std::string objFile = (tempDir / "riscv32.o").string();
  EXPECT_TRUE(env.emitObjectFile(objFile, "riscv32-unknown-elf", "generic-rv32"));
  EXPECT_TRUE(std::filesystem::exists(objFile));

  auto bytes = readFileBytes(objFile);
  ASSERT_GE(bytes.size(), 20u);

  // Validate ELF header: \x7f E L F
  EXPECT_EQ(bytes[0], 0x7F);
  EXPECT_EQ(bytes[1], 'E');
  EXPECT_EQ(bytes[2], 'L');
  EXPECT_EQ(bytes[3], 'F');
  // 32-bit ELF & Little Endian
  EXPECT_EQ(bytes[4], 1); // ELFCLASS32
  EXPECT_EQ(bytes[5], 1); // ELFDATA2LSB
  // e_machine: EM_RISCV = 0xF3 (243)
  uint16_t e_machine = bytes[18] | (bytes[19] << 8);
  EXPECT_EQ(e_machine, 0xF3);
}

TEST_F(CrossCompileTest, TargetWebAssemblyWASI) {
  if (!isTargetSupported("wasm32-unknown-wasi")) {
    GTEST_SKIP() << "Target 'wasm32-unknown-wasi' is not built into the current LLVM library.";
  }

  std::string objFile = (tempDir / "dsp.wasm.o").string();
  EXPECT_TRUE(env.emitObjectFile(objFile, "wasm32-unknown-wasi"));
  EXPECT_TRUE(std::filesystem::exists(objFile));

  auto bytes = readFileBytes(objFile);
  ASSERT_GE(bytes.size(), 8u);

  // Validate WebAssembly binary format magic: \0 a s m
  EXPECT_EQ(bytes[0], 0x00);
  EXPECT_EQ(bytes[1], 'a');
  EXPECT_EQ(bytes[2], 's');
  EXPECT_EQ(bytes[3], 'm');
  // WASM version 1: 0x01, 0x00, 0x00, 0x00
  EXPECT_EQ(bytes[4], 0x01);
  EXPECT_EQ(bytes[5], 0x00);
  EXPECT_EQ(bytes[6], 0x00);
  EXPECT_EQ(bytes[7], 0x00);
}

TEST_F(CrossCompileTest, TargetHostNative) {
  std::string objFile = (tempDir / "native_host.o").string();
  EXPECT_TRUE(env.emitObjectFile(objFile));
  EXPECT_TRUE(std::filesystem::exists(objFile));
  EXPECT_GT(std::filesystem::file_size(objFile), 0);
}

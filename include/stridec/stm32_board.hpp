#pragma once

// What the STM32 runtime needs to know about a board, resolved from the
// board's and device's Stride data (`clocks`, `clockLimits`, `console`,
// `peripherals`, `ports`), validated, and written as stride_board.h.
//
// The clock part covers the STM32F2/F4/F7 RCC: one PLL fed by HSI or HSE,
// AHB/APB dividers, flash wait states and the power settings the speed needs.
// See doc/clocking.md in strideroot for the general clock model this is the
// first step of.

#include <cstdint>
#include <string>

namespace strd {

class ResolvedHardware;

struct Stm32ClockPlan {
  bool pll = false;
  bool hse = false;       // the PLL (or SYSCLK) runs from the crystal
  bool hseBypass = false; // an external clock signal, not a crystal
  int64_t sourceHz = 0;   // HSI or HSE
  int64_t pllM = 0, pllN = 0, pllP = 0, pllQ = 0;
  int64_t ahbDivider = 1, apb1Divider = 1, apb2Divider = 1;
  int64_t sysclkHz = 0, hclkHz = 0, pclk1Hz = 0, pclk2Hz = 0;
  int64_t vcoInHz = 0, vcoOutHz = 0, pllQHz = 0;
  int64_t flashLatency = 0;
  int64_t flashFlags = 0;   // FLASH_ACR accelerator bits
  int64_t voltageScale = 0; // PWR_CR(1) VOS bits
  bool overDrive = false;
};

struct Stm32ConsolePin {
  std::string label;
  int64_t portBase = 0;
  int bit = 0;
  int64_t enableRegister = 0;
  int enableBit = 0;
};

struct Stm32Console {
  bool present = false;
  std::string peripheral;
  int64_t base = 0;
  bool usartV1 = false; // STM32F1/F2/F4 USART (SR/DR); else F7/L4/H7 (ISR/RDR/TDR)
  int64_t clockHz = 0;
  int64_t enableRegister = 0;
  int enableBit = 0;
  int64_t baud = 115200;
  int64_t alternateFunction = 0;
  Stm32ConsolePin tx, rx;
};

struct Stm32Board {
  std::string cpu;
  std::string fpu; // empty: no FPU
  int64_t flashBytes = 0;
  int64_t ramBytes = 0;
  Stm32ClockPlan clock;
  Stm32Console console;
};

// False with a message naming the board and property when the data is
// missing or describes something the hardware can't do.
bool resolveStm32Board(const ResolvedHardware &hw, Stm32Board &out,
                       std::string &error);

// The header the runtime is compiled with (-include stride_board.h).
std::string stm32BoardHeader(const ResolvedHardware &hw,
                             const Stm32Board &board);

} // namespace strd

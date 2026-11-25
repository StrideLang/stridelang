#include "stridec/stm32_board.hpp"

#include "stride/parser/valuenode.h"
#include "stride/stridegen/hardwareresolver.hpp"
#include "stride/utils/astquery.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <sstream>
#include <vector>

namespace strd {

namespace {

std::optional<int64_t> intOf(const ASTNode &node) {
  if (node && node->getNodeType() == AST::Int) {
    return std::static_pointer_cast<ValueNode>(node)->getIntValue();
  }
  return std::nullopt;
}

std::optional<bool> switchOf(const ASTNode &node) {
  if (node && node->getNodeType() == AST::Switch) {
    return std::static_pointer_cast<ValueNode>(node)->getSwitchValue();
  }
  return std::nullopt;
}

ASTNode field(const ASTNode &record, const std::string &key) {
  return record ? ASTQuery::recordValue(record, key) : nullptr;
}

std::string hz(int64_t value) {
  char text[32];
  if (value % 1000000 == 0) {
    std::snprintf(text, sizeof(text), "%lld MHz", (long long)(value / 1000000));
  } else if (value % 1000 == 0) {
    std::snprintf(text, sizeof(text), "%lld kHz", (long long)(value / 1000));
  } else {
    std::snprintf(text, sizeof(text), "%lld Hz", (long long)value);
  }
  return text;
}

// Checks against clockLimits: { <key>: { min: max: values: [...] } }. A key
// the device leaves out has no limit.
class Limits {
public:
  Limits(const ResolvedHardware &hw, std::string &error)
      : m_hw(hw), m_limits(hw.property("clockLimits")), m_error(error) {}

  bool check(const std::string &key, int64_t value,
             const std::string &what) const {
    auto limit = field(m_limits, key);
    if (!limit) {
      return true;
    }
    auto min = intOf(field(limit, "min"));
    auto max = intOf(field(limit, "max"));
    if ((min && value < *min) || (max && value > *max)) {
      m_error = m_hw.name() + ": " + what + " is " + describe(key, value) +
                ", outside clockLimits." + key + " (" +
                (min ? describe(key, *min) : std::string("0")) + " to " +
                (max ? describe(key, *max) : std::string("any")) + ")";
      return false;
    }
    if (auto values = field(limit, "values")) {
      const auto &items = values->getChildren();
      bool allowed = std::any_of(items.begin(), items.end(),
                                 [&](const ASTNode &v) {
                                   return intOf(v) == value;
                                 });
      if (!allowed) {
        m_error = m_hw.name() + ": " + what + " is " + std::to_string(value) +
                  ", not one of clockLimits." + key + ".values";
        return false;
      }
    }
    return true;
  }

  std::optional<int64_t> value(const std::string &key,
                               const std::string &sub) const {
    return intOf(field(field(m_limits, key), sub));
  }

private:
  static std::string describe(const std::string &key, int64_t v) {
    // PLL factors are plain numbers; everything else is a frequency.
    return key.rfind("pll", 0) == 0 ? std::to_string(v) : hz(v);
  }

  const ResolvedHardware &m_hw;
  ASTNode m_limits;
  std::string &m_error;
};

bool resolveClock(const ResolvedHardware &hw, Stm32ClockPlan &plan,
                  std::string &error) {
  const ASTNode clocks = hw.property("clocks");
  if (!clocks) {
    error = hw.name() + " has no clocks: its device must say how the core "
                        "clock is made (clocks: { source: ... })";
    return false;
  }
  auto need = [&](const std::string &key) -> std::optional<int64_t> {
    auto v = intOf(field(clocks, key));
    if (!v) {
      error = hw.name() + ": clocks." + key + " is missing";
    }
    return v;
  };
  Limits limits(hw, error);

  const std::string source = ASTQuery::valueText(field(clocks, "source"));
  if (source == "hse") {
    auto crystal = intOf(field(clocks, "hse"));
    if (!crystal || *crystal <= 0) {
      error = hw.name() + ": clocks.source is \"hse\" but the board gives no "
                          "crystal frequency (clocks: { hse: <Hz> })";
      return false;
    }
    plan.hse = true;
    plan.hseBypass = switchOf(field(clocks, "hseBypass")).value_or(false);
    plan.sourceHz = *crystal;
  } else if (source == "hsi") {
    auto hsi = need("hsi");
    if (!hsi) {
      return false;
    }
    plan.sourceHz = *hsi;
  } else {
    error = hw.name() + ": clocks.source is \"" + source +
            "\"; it must be \"hsi\" or \"hse\"";
    return false;
  }

  plan.pll = switchOf(field(clocks, "pll")).value_or(false);
  if (plan.pll) {
    auto m = need("pllM"), n = need("pllN"), p = need("pllP"),
         q = need("pllQ");
    if (!m || !n || !p || !q) {
      return false;
    }
    plan.pllM = *m;
    plan.pllN = *n;
    plan.pllP = *p;
    plan.pllQ = *q;
    if (!limits.check("pllM", plan.pllM, "clocks.pllM") ||
        !limits.check("pllN", plan.pllN, "clocks.pllN") ||
        !limits.check("pllP", plan.pllP, "clocks.pllP") ||
        !limits.check("pllQ", plan.pllQ, "clocks.pllQ")) {
      return false;
    }
    if (plan.pllM <= 0 || plan.pllP <= 0 || plan.pllQ <= 0) {
      error = hw.name() + ": PLL dividers must be positive";
      return false;
    }
    const int64_t vcoOut = plan.sourceHz * plan.pllN;
    if (plan.sourceHz % plan.pllM != 0 || vcoOut % plan.pllM != 0 ||
        (vcoOut / plan.pllM) % plan.pllP != 0) {
      error = hw.name() + ": " + hz(plan.sourceHz) + " / pllM " +
              std::to_string(plan.pllM) + " * pllN " +
              std::to_string(plan.pllN) + " / pllP " +
              std::to_string(plan.pllP) +
              " is not a whole number of Hz; choose factors that divide evenly";
      return false;
    }
    plan.vcoInHz = plan.sourceHz / plan.pllM;
    plan.vcoOutHz = vcoOut / plan.pllM;
    plan.sysclkHz = plan.vcoOutHz / plan.pllP;
    plan.pllQHz = plan.vcoOutHz / plan.pllQ;
    if (!limits.check("vcoIn", plan.vcoInHz, "the PLL input") ||
        !limits.check("vcoOut", plan.vcoOutHz, "the PLL VCO")) {
      return false;
    }
  } else {
    plan.sysclkHz = plan.sourceHz;
  }

  auto divider = [&](const std::string &key, std::vector<int64_t> allowed,
                     int64_t &out) {
    out = intOf(field(clocks, key)).value_or(1);
    if (std::find(allowed.begin(), allowed.end(), out) == allowed.end()) {
      error = hw.name() + ": clocks." + key + " is " + std::to_string(out) +
              "; the RCC divides by 1, 2, 4, 8, 16" +
              (allowed.size() > 5 ? ", 64, 128, 256 or 512" : "");
      return false;
    }
    return true;
  };
  if (!divider("ahbDivider", {1, 2, 4, 8, 16, 64, 128, 256, 512},
               plan.ahbDivider) ||
      !divider("apb1Divider", {1, 2, 4, 8, 16}, plan.apb1Divider) ||
      !divider("apb2Divider", {1, 2, 4, 8, 16}, plan.apb2Divider)) {
    return false;
  }
  plan.hclkHz = plan.sysclkHz / plan.ahbDivider;
  plan.pclk1Hz = plan.hclkHz / plan.apb1Divider;
  plan.pclk2Hz = plan.hclkHz / plan.apb2Divider;
  if (!limits.check("sysclk", plan.sysclkHz, "SYSCLK") ||
      !limits.check("ahb", plan.hclkHz, "the AHB clock (HCLK)") ||
      !limits.check("apb1", plan.pclk1Hz, "the APB1 clock") ||
      !limits.check("apb2", plan.pclk2Hz, "the APB2 clock")) {
    return false;
  }

  // One wait state per clockLimits.flashLatency.stepHz of HCLK.
  if (auto step = limits.value("flashLatency", "stepHz"); step && *step > 0) {
    plan.flashLatency = (plan.hclkHz + *step - 1) / *step - 1;
    auto maxLatency = limits.value("flashLatency", "max");
    if (maxLatency && plan.flashLatency > *maxLatency) {
      error = hw.name() + ": HCLK " + hz(plan.hclkHz) + " needs " +
              std::to_string(plan.flashLatency) +
              " flash wait states, more than the " +
              std::to_string(*maxLatency) + " the flash allows";
      return false;
    }
  }
  plan.flashFlags = intOf(field(clocks, "flashFlags")).value_or(0);
  plan.voltageScale = intOf(field(clocks, "voltageScale")).value_or(0);
  if (auto above = limits.value("overDrive", "above")) {
    plan.overDrive = plan.hclkHz > *above;
  }

  // `clock` is what the program is told the core runs at; it must be the
  // clock the runtime really sets up.
  auto declared = hw.intProperty("clock");
  if (!declared || *declared != plan.hclkHz) {
    error = hw.name() + ": clock is " +
            (declared ? hz(*declared) : std::string("missing")) +
            " but its clocks make HCLK " + hz(plan.hclkHz) + "; set clock: " +
            std::to_string(plan.hclkHz);
    return false;
  }
  return true;
}

bool resolvePin(const ResolvedHardware &hw, const std::string &role,
                Stm32ConsolePin &pin, std::string &error) {
  pin.label = hw.recordString("console", role, "");
  auto boardPin = pin.label.empty() ? std::nullopt : hw.boardPin(pin.label);
  if (!boardPin) {
    error = hw.name() + ": console." + role +
            (pin.label.empty() ? " is missing"
                               : " names no pin of the board: " + pin.label);
    return false;
  }
  auto port = hw.gpioPort(boardPin->port);
  if (!port || !port->base || !port->enableRegister) {
    error = hw.name() + ": console." + role + " is on port " +
            boardPin->port +
            ", which needs a base address (gpioPort " + boardPin->port +
            " @ 0x...) and an enable: { register: bit: }";
    return false;
  }
  pin.portBase = *port->base;
  pin.bit = boardPin->bit;
  pin.enableRegister = *port->enableRegister;
  pin.enableBit = port->enableBit;
  return true;
}

bool resolveConsole(const ResolvedHardware &hw, const Stm32ClockPlan &clock,
                    Stm32Console &console, std::string &error) {
  if (!hw.property("console")) {
    return true; // no console: Print goes nowhere
  }
  console.present = true;
  console.peripheral = hw.recordString("console", "peripheral", "");
  auto usart = hw.peripheral(console.peripheral);
  if (!usart) {
    error = hw.name() + ": console.peripheral " +
            (console.peripheral.empty() ? std::string("is missing")
                                        : console.peripheral +
                                              " is not in the device's "
                                              "peripherals");
    return false;
  }
  if (usart->kind != "uart" || !usart->base || !usart->enableRegister) {
    error = hw.name() + ": console.peripheral " + console.peripheral +
            " must be a uart with an address (@ 0x...) and an enable";
    return false;
  }
  if (usart->ip == "stm32.usart.v1") {
    console.usartV1 = true;
  } else if (usart->ip != "stm32.usart.v2") {
    error = hw.name() + ": the STM32 runtime has no console driver for " +
            console.peripheral + "'s ip \"" + usart->ip +
            "\" (it knows stm32.usart.v1 and stm32.usart.v2)";
    return false;
  }
  if (usart->bus == "apb1") {
    console.clockHz = clock.pclk1Hz;
  } else if (usart->bus == "apb2") {
    console.clockHz = clock.pclk2Hz;
  } else {
    error = hw.name() + ": " + console.peripheral + "'s bus is \"" +
            usart->bus + "\"; the STM32 runtime clocks a uart from \"apb1\" "
                         "or \"apb2\"";
    return false;
  }
  console.base = *usart->base;
  console.enableRegister = *usart->enableRegister;
  console.enableBit = usart->enableBit;
  console.baud = hw.recordInt("console", "baud").value_or(115200);
  auto af = hw.recordInt("console", "select");
  if (!af || *af < 0 || *af > 15) {
    error = hw.name() + ": console.select must be the pins' alternate "
                        "function number, 0 to 15";
    return false;
  }
  console.alternateFunction = *af;
  if (console.baud <= 0 || console.clockHz / console.baud < 16) {
    error = hw.name() + ": " + console.peripheral + " can't run at " +
            std::to_string(console.baud) + " baud from " +
            hz(console.clockHz);
    return false;
  }
  return resolvePin(hw, "tx", console.tx, error) &&
         resolvePin(hw, "rx", console.rx, error);
}

} // namespace

bool resolveStm32Board(const ResolvedHardware &hw, Stm32Board &out,
                       std::string &error) {
  out = {};
  out.cpu = hw.stringProperty("cpu");
  out.fpu = hw.stringProperty("fpu");
  out.flashBytes = hw.recordInt("memory", "flash").value_or(0);
  out.ramBytes = hw.recordInt("memory", "ram").value_or(0);
  if (out.cpu.empty()) {
    error = hw.name() + " has no cpu (e.g. cpu: \"cortex-m4\")";
    return false;
  }
  if (out.flashBytes <= 0 || out.ramBytes <= 0) {
    error = hw.name() + " needs memory: { flash: <bytes> ram: <bytes> }";
    return false;
  }
  return resolveClock(hw, out.clock, error) &&
         resolveConsole(hw, out.clock, out.console, error);
}

std::string stm32BoardHeader(const ResolvedHardware &hw,
                             const Stm32Board &board) {
  std::ostringstream h;
  auto hex = [](int64_t v) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%08llXUL", (unsigned long long)v);
    return std::string(text);
  };
  const auto &c = board.clock;
  h << "/* Generated by stride build for " << hw.name()
    << " from its Stride hardware data. Do not edit. */\n"
    << "#ifndef STRIDE_BOARD_H\n#define STRIDE_BOARD_H\n\n"
    << "/* Clocks */\n"
    << "#define STRIDE_SYSCLK_HZ " << c.sysclkHz << "UL\n"
    << "#define STRIDE_HCLK_HZ " << c.hclkHz << "UL\n"
    << "#define STRIDE_PCLK1_HZ " << c.pclk1Hz << "UL\n"
    << "#define STRIDE_PCLK2_HZ " << c.pclk2Hz << "UL\n"
    << "#define STRIDE_CLOCK_HSE " << (c.hse ? 1 : 0) << "\n"
    << "#define STRIDE_CLOCK_HSE_BYPASS " << (c.hseBypass ? 1 : 0) << "\n"
    << "#define STRIDE_CLOCK_PLL " << (c.pll ? 1 : 0) << "\n"
    << "#define STRIDE_PLL_M " << c.pllM << "\n"
    << "#define STRIDE_PLL_N " << c.pllN << "\n"
    << "#define STRIDE_PLL_P " << c.pllP << "\n"
    << "#define STRIDE_PLL_Q " << c.pllQ << "\n"
    << "#define STRIDE_AHB_DIVIDER " << c.ahbDivider << "\n"
    << "#define STRIDE_APB1_DIVIDER " << c.apb1Divider << "\n"
    << "#define STRIDE_APB2_DIVIDER " << c.apb2Divider << "\n"
    << "#define STRIDE_FLASH_LATENCY " << c.flashLatency << "\n"
    << "#define STRIDE_FLASH_FLAGS " << hex(c.flashFlags) << "\n"
    << "#define STRIDE_VOLTAGE_SCALE " << hex(c.voltageScale) << "\n"
    << "#define STRIDE_OVERDRIVE " << (c.overDrive ? 1 : 0) << "\n\n";

  const auto &k = board.console;
  h << "/* Console */\n"
    << "#define STRIDE_CONSOLE " << (k.present ? 1 : 0) << "\n";
  if (k.present) {
    h << "/* " << k.peripheral << " on " << k.tx.label << " (tx) and "
      << k.rx.label << " (rx) */\n"
      << "#define STRIDE_CONSOLE_BASE " << hex(k.base) << "\n"
      << "#define STRIDE_CONSOLE_USART_V1 " << (k.usartV1 ? 1 : 0) << "\n"
      << "#define STRIDE_CONSOLE_CLOCK_HZ " << k.clockHz << "UL\n"
      << "#define STRIDE_CONSOLE_BAUD " << k.baud << "UL\n"
      << "#define STRIDE_CONSOLE_ENABLE_REG " << hex(k.enableRegister) << "\n"
      << "#define STRIDE_CONSOLE_ENABLE_BIT " << k.enableBit << "\n"
      << "#define STRIDE_CONSOLE_AF " << k.alternateFunction << "\n";
    for (const auto *pin : {&k.tx, &k.rx}) {
      const char *p = pin == &k.tx ? "TX" : "RX";
      h << "#define STRIDE_CONSOLE_" << p << "_PORT " << hex(pin->portBase)
        << "\n"
        << "#define STRIDE_CONSOLE_" << p << "_PIN " << pin->bit << "\n"
        << "#define STRIDE_CONSOLE_" << p << "_ENABLE_REG "
        << hex(pin->enableRegister) << "\n"
        << "#define STRIDE_CONSOLE_" << p << "_ENABLE_BIT " << pin->enableBit
        << "\n";
    }
  }
  h << "\n#endif\n";
  return h.str();
}

} // namespace strd

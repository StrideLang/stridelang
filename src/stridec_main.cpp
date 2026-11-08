#include "stridec/driver.hpp"

#include <string>
#include <vector>

int main(int argc, char *argv[]) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  return strd::runStridec(args);
}

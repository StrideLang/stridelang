#pragma once

#include <iostream>
#include <string>
#include <vector>

namespace strd {

struct StridecOptions {
  std::string inputFile;
  std::string outputFile;
  std::string headerFile;
  std::string targetTriple;
  std::string targetCPU;
  std::string targetFeatures;
  std::string relocModel;
  std::string codeModel;
  std::string domainName;
  std::string strideRoot;
  std::vector<std::string> includeDirs;
  bool verbose = false;
  bool showHelp = false;
  bool showVersion = false;
};

void printHelp(std::ostream &os, const char *progName = "stridec");
void printVersion(std::ostream &os);

bool parseCommandLine(const std::vector<std::string> &args, StridecOptions &opts,
                      std::string &errorMessage);

int runStridec(const std::vector<std::string> &args,
               std::ostream &out = std::cout, std::ostream &err = std::cerr);

} // namespace strd

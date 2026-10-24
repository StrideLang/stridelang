/*
    Stride is licensed under the terms of the GNU Affero General Public License version 3.

    Copyright (C) 2017-2026 Andres Cabrera
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

    Author: Andres Cabrera
*/

#pragma once

#include <iostream>
#include <string>
#include <vector>

namespace strd {

struct StridecOptions {
  std::vector<std::string> inputFiles;
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
  // Arduino pre-built library options
  std::string arduinoLibDir;
  std::string arduinoLibName;
  std::string arduinoLibArch = "arm";
  std::string arduinoLibMcu;
  std::string arduinoAuthor = "Stride";
  std::string arduinoVersion = "1.0.0";
  bool emitAllFunctions = false;
  bool debugInfo = false;
  std::string debugInfoLevel = "none";
  int optimizationLevel = -1;
  std::string buildType;
  bool vectorize = false;
  bool noVectorize = false;
  bool unrollLoops = false;
  bool noUnrollLoops = false;
  bool verbose = false;
  bool showHelp = false;
  bool showVersion = false;
};

struct ArduinoTarget {
  std::string mcu;
  std::string triple;
  std::string cpu;
  std::string features;
  std::string subfolder;
};

std::vector<ArduinoTarget> getSupportedArduinoTargets(const std::string &arch = "arm");

bool writeStaticArchive(const std::string &archivePath,
                        const std::vector<std::string> &memberPaths,
                        std::string &errorMsg);

bool emitArduinoLibrary(const StridecOptions &opts,
                        std::ostream &out = std::cout,
                        std::ostream &err = std::cerr);

void printHelp(std::ostream &os, const char *progName = "stridec");
void printVersion(std::ostream &os);

bool parseCommandLine(const std::vector<std::string> &args, StridecOptions &opts,
                      std::string &errorMessage);

int runStridec(const std::vector<std::string> &args,
               std::ostream &out = std::cout, std::ostream &err = std::cerr);

} // namespace strd

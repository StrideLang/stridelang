cmake_minimum_required(VERSION 3.13)
project(stridelang VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies -------------------------------------------------------------
if (NOT TARGET StrideJit)
    # 1. Check for prebuilt binaries from an existing build directory
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindPrebuiltStride.cmake")

    if (NOT STRIDE_PREBUILT_FOUND)
        # 2. Fallback: Automatic local sibling discovery to build from source
        if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../strideparser/CMakeLists.txt" AND
            EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../stridejit/CMakeLists.txt")
            message(STATUS "stridelang: building dependencies from local source subdirectories")
            if (NOT TARGET StrideParser)
                add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../strideparser" strideparser_build)
            endif()
            if (NOT TARGET StrideUtils)
                add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../strideutils" strideutils_build)
            endif()
            if (NOT TARGET StrideGen)
                add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../stridegen" stridegen_build)
            endif()
            if (NOT TARGET StrideJit)
                add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../stridejit" stridejit_build)
            endif()
        else()
            # 3. Fallback: Installed package resolution
            find_package(StrideJit CONFIG REQUIRED)
            find_package(StrideParser CONFIG REQUIRED)
            find_package(StrideUtils CONFIG REQUIRED)
            find_package(StrideGen CONFIG REQUIRED)
        endif()
    endif()
endif()

# Compiler Driver Library & Executables (stride & stridec) -----------------
add_library(stridec_driver STATIC
    src/driver.cpp
)

target_include_directories(stridec_driver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(stridec_driver PUBLIC
    StrideJit
    StrideParser
    StrideUtils
    StrideGen
)

# Unified Stride CLI Front-End
add_executable(stride
    src/stride_main.cpp
)

target_link_libraries(stride PRIVATE
    stridec_driver
    StrideGen
    StrideParser
    StrideUtils
    StrideJit
)

# Compatibility AOT Compiler Executable (stridec)
add_executable(stridec
    src/stridec_main.cpp
)

target_link_libraries(stridec PRIVATE
    stridec_driver
)

# Exported Aliases
add_executable(Stride::Compiler ALIAS stridec)
add_executable(Stride::Toolchain ALIAS stride)
if(TARGET StrideRuntime AND NOT TARGET Stride::Runtime)
    add_library(Stride::Runtime ALIAS StrideRuntime)
endif()

# Installation & CMake Package Configuration -------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Generate StrideConfigVersion.cmake
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/StrideConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install executable targets
install(TARGETS stride stridec
    EXPORT StrideTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Compiler
)

# Standard Library & Systems (STRIDEROOT) Discovery & Packaging
if (NOT STRIDEROOT)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../strideroot")
        set(STRIDEROOT "${CMAKE_CURRENT_SOURCE_DIR}/../strideroot" CACHE PATH "Path to strideroot")
    elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../strideroot")
        set(STRIDEROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../strideroot" CACHE PATH "Path to strideroot")
    elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../strideroot")
        set(STRIDEROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../strideroot" CACHE PATH "Path to strideroot")
    elseif (DEFINED ENV{STRIDEROOT})
        set(STRIDEROOT "$ENV{STRIDEROOT}" CACHE PATH "Path to strideroot")
    endif()
endif()

if (STRIDEROOT AND EXISTS "${STRIDEROOT}")
    message(STATUS "stridelang: packaging standard library from ${STRIDEROOT}")
    install(DIRECTORY "${STRIDEROOT}/"
        DESTINATION strideroot
        COMPONENT StandardLibrary
        PATTERN ".git*" EXCLUDE
    )
endif()

# Bundled VS Code Extension Packaging
set(VSIX_FILE "")
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../vscode-stride-lang")
    file(GLOB VSIX_CANDIDATES "${CMAKE_CURRENT_SOURCE_DIR}/../vscode-stride-lang/*.vsix")
    if (VSIX_CANDIDATES)
        list(GET VSIX_CANDIDATES 0 VSIX_FILE)
    endif()
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../vscode-stride-lang")
    file(GLOB VSIX_CANDIDATES "${CMAKE_CURRENT_SOURCE_DIR}/../../vscode-stride-lang/*.vsix")
    if (VSIX_CANDIDATES)
        list(GET VSIX_CANDIDATES 0 VSIX_FILE)
    endif()
endif()
if (VSIX_FILE AND EXISTS "${VSIX_FILE}")
    message(STATUS "stridelang: bundling VS Code extension ${VSIX_FILE}")
    install(FILES "${VSIX_FILE}"
        RENAME "stride-language.vsix"
        DESTINATION vscode
        COMPONENT VSCode
    )
endif()

if(TARGET StrideRuntime)
    install(TARGETS StrideRuntime
        EXPORT StrideTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT Development
    )
endif()

# Export CMake target definitions
install(EXPORT StrideTargets
    FILE StrideTargets.cmake
    NAMESPACE Stride::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Stride
    COMPONENT Development
)

# Install CMake modules & package config files
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StrideConfig.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindStride.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StrideFunctions.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/StrideConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Stride
    COMPONENT Development
)

# Install example firmware projects
install(DIRECTORY examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/stride/examples
    COMPONENT Examples
)

# Install documentation
install(FILES README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT Documentation
)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/doc")
    install(DIRECTORY doc/
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
        COMPONENT Documentation
    )
endif()

# Testing ------------------------------------------------------------------
option(STRIDE_BUILD_TESTS "Build stridelang test suite" ON)

if (STRIDE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# CPack Installer Packaging ------------------------------------------------
set(CPACK_PACKAGE_NAME "stridelang")
set(CPACK_PACKAGE_VENDOR "Stride Language Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Stride Ahead-Of-Time Compiler & Embedded Toolchain")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/StrideLang/stridelang")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Stride")
set(CPACK_STRIP_FILES TRUE)

# Component grouping
set(CPACK_COMPONENTS_ALL Compiler StandardLibrary VSCode Development Examples Documentation)
set(CPACK_COMPONENT_COMPILER_DISPLAY_NAME "Stride Unified Toolchain (stride & stridec)")
set(CPACK_COMPONENT_COMPILER_DESCRIPTION "Compiler, runtime driver, and toolchain manager binaries.")
set(CPACK_COMPONENT_COMPILER_REQUIRED ON)

set(CPACK_COMPONENT_STANDARDLIBRARY_DISPLAY_NAME "Stride Standard Library & Systems")
set(CPACK_COMPONENT_STANDARDLIBRARY_DESCRIPTION "Core standard library, systems, and platforms (STRIDEROOT).")
set(CPACK_COMPONENT_STANDARDLIBRARY_REQUIRED ON)

set(CPACK_COMPONENT_VSCODE_DISPLAY_NAME "Visual Studio Code Extension")
set(CPACK_COMPONENT_VSCODE_DESCRIPTION "Rich language support, LSP client, and debugging integration for VS Code.")

set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "CMake Integration Modules")
set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION "CMake find_package(Stride) & stride_target_sources() integration.")
set(CPACK_COMPONENT_DEVELOPMENT_REQUIRED ON)

set(CPACK_COMPONENT_EXAMPLES_DISPLAY_NAME "Embedded Firmware Examples")
set(CPACK_COMPONENT_EXAMPLES_DESCRIPTION "Raspberry Pi Pico, STM32, and bare-metal MCU project examples.")

set(CPACK_COMPONENT_DOCUMENTATION_DISPLAY_NAME "Documentation")

# Windows NSIS (.exe installer) configuration
set(CPACK_NSIS_DISPLAY_NAME "Stride Language Toolchain")
set(CPACK_NSIS_PACKAGE_NAME "Stride Language Toolchain")
set(CPACK_NSIS_MODIFY_PATH ON) # Add bin directory to system PATH environment variable
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_HELP_LINK "https://github.com/StrideLang/stridelang")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/StrideLang/stridelang")

set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
  WriteRegStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "STRIDEROOT" "$INSTDIR\strideroot"
  WriteRegStr HKCU "Environment" "STRIDEROOT" "$INSTDIR\strideroot"
  SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000

  ClearErrors
  SearchPath $R0 "code.cmd"
  IfErrors 0 InstallVsCodeExt
  SearchPath $R0 "code.exe"
  IfErrors 0 InstallVsCodeExt
  IfFileExists "$LOCALAPPDATA\Programs\Microsoft VS Code\bin\code.cmd" 0 SkipVsCodeExt
  StrCpy $R0 "$LOCALAPPDATA\Programs\Microsoft VS Code\bin\code.cmd"

InstallVsCodeExt:
  IfFileExists "$INSTDIR\vscode\stride-language.vsix" 0 SkipVsCodeExt
  DetailPrint "Installing Stride VS Code Extension..."
  ExecWait '"$R0" --install-extension "$INSTDIR\vscode\stride-language.vsix" --force'

SkipVsCodeExt:
]=])

set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
  DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "STRIDEROOT"
  DeleteRegValue HKCU "Environment" "STRIDEROOT"
  SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000
]=])

# Package generators per platform
if (WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
elseif (APPLE)
    set(CPACK_GENERATOR "TGZ;ZIP")
else ()
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
endif ()

include(CPack)

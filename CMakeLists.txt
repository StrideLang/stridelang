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

# Compiler Driver Library & Executable (stridec) --------------------------
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

add_executable(stridec
    src/main.cpp
)

target_link_libraries(stridec PRIVATE
    stridec_driver
)

# Exported Aliases
add_executable(Stride::Compiler ALIAS stridec)
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

# Install executable target
install(TARGETS stridec
    EXPORT StrideTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Compiler
)

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
set(CPACK_COMPONENTS_ALL Compiler Development Examples Documentation)
set(CPACK_COMPONENT_COMPILER_DISPLAY_NAME "Stride Compiler (stridec)")
set(CPACK_COMPONENT_COMPILER_DESCRIPTION "Ahead-Of-Time compiler binary for Stride.")
set(CPACK_COMPONENT_COMPILER_REQUIRED ON)

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

# Package generators per platform
if (WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
elseif (APPLE)
    set(CPACK_GENERATOR "TGZ;ZIP")
else ()
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
endif ()

include(CPack)


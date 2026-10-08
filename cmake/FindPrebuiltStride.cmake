#[=======================================================================[.rst:
FindPrebuiltStride
------------------
Locates prebuilt Stride and LLVM libraries from an existing build directory
to avoid recompiling LLVM and Stride from source.

User-configurable variables:
  STRIDE_PREBUILT_BUILD_DIR - Path to build directory containing compiled .lib / .a files
  STRIDE_SRC_DIR            - Path to directory containing source repos (default: ..)

Outputs:
  STRIDE_PREBUILT_FOUND     - True if all prebuilt libraries and headers were resolved
  Imported targets: StrideParser, StrideUtils, StrideCodegen, StrideJit
#]=======================================================================]

set(STRIDE_PREBUILT_FOUND FALSE)

# 1. Resolve Source Directory
if(NOT STRIDE_SRC_DIR)
    if(DEFINED ENV{STRIDE_SRC_DIR})
        set(STRIDE_SRC_DIR "$ENV{STRIDE_SRC_DIR}")
    else()
        get_filename_component(STRIDE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    endif()
endif()

# 2. Resolve Build Directory
if(NOT STRIDE_PREBUILT_BUILD_DIR)
    if(DEFINED ENV{STRIDE_PREBUILT_BUILD_DIR})
        set(STRIDE_PREBUILT_BUILD_DIR "$ENV{STRIDE_PREBUILT_BUILD_DIR}")
    else()
        # Search common build directory patterns
        file(GLOB CANDIDATE_BUILD_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../../build/*"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/*"
            "${STRIDE_SRC_DIR}/stridejit/build"
            "${CMAKE_CURRENT_SOURCE_DIR}/build"
        )
        foreach(CANDIDATE IN LISTS CANDIDATE_BUILD_DIRS)
            if(EXISTS "${CANDIDATE}/libgame/external/stridejit" OR EXISTS "${CANDIDATE}/src/stridejit")
                set(STRIDE_PREBUILT_BUILD_DIR "${CANDIDATE}")
                break()
            endif()
        endforeach()
    endif()
endif()

if(NOT STRIDE_PREBUILT_BUILD_DIR OR NOT EXISTS "${STRIDE_PREBUILT_BUILD_DIR}")
    message(STATUS "FindPrebuiltStride: No prebuilt build directory specified or found.")
    return()
endif()

message(STATUS "FindPrebuiltStride: Searching for prebuilt binaries in: ${STRIDE_PREBUILT_BUILD_DIR}")
message(STATUS "FindPrebuiltStride: Using source headers from: ${STRIDE_SRC_DIR}")

# 3. Locate Stride Libraries
find_library(PREBUILT_STRIDE_JIT
    NAMES StrideJit libStrideJit
    HINTS
        "${STRIDE_PREBUILT_BUILD_DIR}/libgame/external/stridejit"
        "${STRIDE_PREBUILT_BUILD_DIR}/stridejit"
        "${STRIDE_PREBUILT_BUILD_DIR}/lib"
    PATH_SUFFIXES Debug Release RelWithDebInfo .
    NO_DEFAULT_PATH
)

find_library(PREBUILT_STRIDE_PARSER
    NAMES StrideParser libStrideParser
    HINTS
        "${STRIDE_PREBUILT_BUILD_DIR}/libgame/external/strideparser"
        "${STRIDE_PREBUILT_BUILD_DIR}/strideparser"
        "${STRIDE_PREBUILT_BUILD_DIR}/lib"
    PATH_SUFFIXES Debug Release RelWithDebInfo .
    NO_DEFAULT_PATH
)

find_library(PREBUILT_STRIDE_UTILS
    NAMES StrideUtils libStrideUtils
    HINTS
        "${STRIDE_PREBUILT_BUILD_DIR}/libgame/external/strideutils"
        "${STRIDE_PREBUILT_BUILD_DIR}/strideutils"
        "${STRIDE_PREBUILT_BUILD_DIR}/lib"
    PATH_SUFFIXES Debug Release RelWithDebInfo .
    NO_DEFAULT_PATH
)

find_library(PREBUILT_STRIDE_CODEGEN
    NAMES StrideCodegen libStrideCodegen
    HINTS
        "${STRIDE_PREBUILT_BUILD_DIR}/libgame/external/codegen"
        "${STRIDE_PREBUILT_BUILD_DIR}/codegen"
        "${STRIDE_PREBUILT_BUILD_DIR}/lib"
    PATH_SUFFIXES Debug Release RelWithDebInfo .
    NO_DEFAULT_PATH
)

if(NOT (PREBUILT_STRIDE_JIT AND PREBUILT_STRIDE_PARSER AND PREBUILT_STRIDE_UTILS AND PREBUILT_STRIDE_CODEGEN))
    message(STATUS "FindPrebuiltStride: Could not locate all required Stride .lib files.")
    return()
endif()

# 4. Locate Prebuilt LLVM Libraries & Headers
set(LLVM_SEARCH_DIRS
    "${STRIDE_PREBUILT_BUILD_DIR}/_deps/llvm-project-build"
    "${STRIDE_PREBUILT_BUILD_DIR}/_deps/llvm-project-src"
    "${STRIDE_PREBUILT_BUILD_DIR}/llvm"
)

set(PREBUILT_LLVM_INCLUDES "")
if(EXISTS "${STRIDE_PREBUILT_BUILD_DIR}/_deps/llvm-project-src/llvm/include")
    list(APPEND PREBUILT_LLVM_INCLUDES "${STRIDE_PREBUILT_BUILD_DIR}/_deps/llvm-project-src/llvm/include")
endif()
if(EXISTS "${STRIDE_PREBUILT_BUILD_DIR}/_deps/llvm-project-build/include")
    list(APPEND PREBUILT_LLVM_INCLUDES "${STRIDE_PREBUILT_BUILD_DIR}/_deps/llvm-project-build/include")
endif()
if(EXISTS "${STRIDE_PREBUILT_BUILD_DIR}/_deps/llvm-project-build/llvm/include")
    list(APPEND PREBUILT_LLVM_INCLUDES "${STRIDE_PREBUILT_BUILD_DIR}/_deps/llvm-project-build/llvm/include")
endif()

set(LLVM_COMPONENTS
    LLVMSupport LLVMCore LLVMIRReader LLVMOrcJIT LLVMExecutionEngine LLVMMIRParser
    LLVMAArch64AsmParser LLVMAArch64CodeGen LLVMX86AsmParser LLVMX86CodeGen
    LLVMARMAsmParser LLVMARMCodeGen LLVMWebAssemblyAsmParser LLVMWebAssemblyCodeGen
)

set(PREBUILT_LLVM_LIBS "")
foreach(COMP IN LISTS LLVM_COMPONENTS)
    find_library(LIB_${COMP}
        NAMES ${COMP} lib${COMP}
        HINTS
            "${STRIDE_PREBUILT_BUILD_DIR}/_deps/llvm-project-build/lib"
            "${STRIDE_PREBUILT_BUILD_DIR}/_deps/llvm-project-build/lib/Debug"
            "${STRIDE_PREBUILT_BUILD_DIR}/_deps/llvm-project-build/lib/Release"
            "${STRIDE_PREBUILT_BUILD_DIR}/lib"
        NO_DEFAULT_PATH
    )
    if(LIB_${COMP})
        list(APPEND PREBUILT_LLVM_LIBS "${LIB_${COMP}}")
    endif()
endforeach()

# 5. Define Imported Targets
if(NOT TARGET StrideParser)
    add_library(StrideParser STATIC IMPORTED GLOBAL)
    set_target_properties(StrideParser PROPERTIES
        IMPORTED_LOCATION "${PREBUILT_STRIDE_PARSER}"
        INTERFACE_INCLUDE_DIRECTORIES "${STRIDE_SRC_DIR}/strideparser/include"
    )
endif()

if(NOT TARGET StrideUtils)
    add_library(StrideUtils STATIC IMPORTED GLOBAL)
    set_target_properties(StrideUtils PROPERTIES
        IMPORTED_LOCATION "${PREBUILT_STRIDE_UTILS}"
        INTERFACE_INCLUDE_DIRECTORIES "${STRIDE_SRC_DIR}/strideutils/include"
    )
endif()

if(NOT TARGET StrideCodegen)
    add_library(StrideCodegen STATIC IMPORTED GLOBAL)
    set_target_properties(StrideCodegen PROPERTIES
        IMPORTED_LOCATION "${PREBUILT_STRIDE_CODEGEN}"
        INTERFACE_INCLUDE_DIRECTORIES "${STRIDE_SRC_DIR}/codegen/include"
    )
endif()

if(NOT TARGET StrideJit)
    add_library(StrideJit STATIC IMPORTED GLOBAL)
    set_target_properties(StrideJit PROPERTIES
        IMPORTED_LOCATION "${PREBUILT_STRIDE_JIT}"
        INTERFACE_INCLUDE_DIRECTORIES "${STRIDE_SRC_DIR}/stridejit/include;${PREBUILT_LLVM_INCLUDES}"
        INTERFACE_LINK_LIBRARIES "StrideParser;StrideUtils;StrideCodegen;${PREBUILT_LLVM_LIBS}"
    )
endif()

set(STRIDE_PREBUILT_FOUND TRUE)
message(STATUS "FindPrebuiltStride: Successfully imported prebuilt StrideJit, Parser, Utils, and Codegen targets!")

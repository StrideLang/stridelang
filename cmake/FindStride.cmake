#[=======================================================================[.rst:
FindStride
----------
Finds the Stride Ahead-Of-Time compiler (stridec) and exports helper functions.

Result Variables:
  STRIDE_FOUND                - True if the Stride compiler was found
  STRIDE_COMPILER_EXECUTABLE  - Path to the Stride compiler executable
  STRIDE_VERSION              - Version of the Stride compiler

Imported Targets:
  Stride::Compiler            - Executable target pointing to the compiler

Commands Provided:
  stride_target_sources       - Compiles .stride sources to .o + .h and attaches to target
  stride_add_library          - Creates a static or object library from .stride sources
#]=======================================================================]

# 1. Check if stridec target exists in current CMake build tree
if(TARGET stridec)
    set(STRIDE_FOUND TRUE)
    set(STRIDE_COMPILER_EXECUTABLE "$<TARGET_FILE:stridec>")
    if(NOT TARGET Stride::Compiler)
        add_executable(Stride::Compiler ALIAS stridec)
    endif()
else()
    # 2. Search host paths and environment
    find_program(STRIDE_COMPILER_EXECUTABLE
        NAMES stridec stride
        HINTS
            ${STRIDE_ROOT}
            ENV STRIDE_ROOT
        PATH_SUFFIXES bin
        DOC "Stride Ahead-Of-Time (AOT) compiler executable"
    )

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(Stride
        REQUIRED_VARS STRIDE_COMPILER_EXECUTABLE
    )

    if(STRIDE_FOUND AND NOT TARGET Stride::Compiler)
        add_executable(Stride::Compiler IMPORTED GLOBAL)
        set_target_properties(Stride::Compiler PROPERTIES
            IMPORTED_LOCATION "${STRIDE_COMPILER_EXECUTABLE}"
        )
    endif()
endif()

# 3. Export Stride::Runtime static library target if available
if(TARGET StrideRuntime)
    if(NOT TARGET Stride::Runtime)
        add_library(Stride::Runtime ALIAS StrideRuntime)
    endif()
else()
    find_library(STRIDE_RUNTIME_LIBRARY
        NAMES StrideRuntime libStrideRuntime
        HINTS
            ${STRIDE_ROOT}
            ENV STRIDE_ROOT
        PATH_SUFFIXES lib lib64
        DOC "Stride Runtime static library"
    )
    if(STRIDE_RUNTIME_LIBRARY AND NOT TARGET Stride::Runtime)
        add_library(Stride::Runtime STATIC IMPORTED GLOBAL)
        set_target_properties(Stride::Runtime PROPERTIES
            IMPORTED_LOCATION "${STRIDE_RUNTIME_LIBRARY}"
        )
    endif()
endif()

# 4. Define Stride::PlatformLibs interface target for transitive platform dependencies
if(NOT TARGET Stride::PlatformLibs)
    add_library(Stride::PlatformLibs INTERFACE IMPORTED GLOBAL)
    if(NOT MSVC AND NOT CMAKE_CROSSCOMPILING)
        find_library(_STRIDE_LIBM m)
        if(_STRIDE_LIBM)
            set_property(TARGET Stride::PlatformLibs APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${_STRIDE_LIBM})
        else()
            set_property(TARGET Stride::PlatformLibs APPEND PROPERTY INTERFACE_LINK_LIBRARIES m)
        endif()
    endif()
endif()

if(TARGET Stride::Runtime)
    set_property(TARGET Stride::Runtime APPEND PROPERTY INTERFACE_LINK_LIBRARIES Stride::PlatformLibs)
endif()

if(STRIDE_FOUND)
    include("${CMAKE_CURRENT_LIST_DIR}/StrideFunctions.cmake")
endif()

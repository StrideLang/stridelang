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

if(STRIDE_FOUND)
    include("${CMAKE_CURRENT_LIST_DIR}/StrideFunctions.cmake")
endif()

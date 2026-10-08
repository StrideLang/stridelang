#[=======================================================================[.rst:
StrideFunctions
---------------
Helper CMake functions for integrating Stride files into build targets.
#]=======================================================================]

function(stride_target_sources TARGET VISIBILITY)
    if(NOT ARGN)
        message(FATAL_ERROR "stride_target_sources requires at least one .stride source file.")
    endif()

    set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/stride_gen_${TARGET}")
    set(GEN_INC_DIR "${GEN_DIR}/include")
    file(MAKE_DIRECTORY "${GEN_INC_DIR}")

    # Determine compiler command and dependency
    if(DEFINED STRIDE_COMPILER_BIN)
        set(STRIDE_COMPILER_CMD "${STRIDE_COMPILER_BIN}")
        set(STRIDE_COMPILER_DEP "${STRIDE_COMPILER_BIN}")
    elseif(TARGET Stride::Compiler)
        set(STRIDE_COMPILER_CMD Stride::Compiler)
        set(STRIDE_COMPILER_DEP Stride::Compiler)
    else()
        set(STRIDE_COMPILER_CMD stridec)
        set(STRIDE_COMPILER_DEP "")
    endif()

    set(OBJ_EXT "${CMAKE_C_OUTPUT_EXTENSION}")
    if(NOT OBJ_EXT)
        set(OBJ_EXT ".o")
    endif()

    # Deduce Target Triple & CPU for cross-compilation if not explicitly set
    set(COMPILER_ARGS "")

    if(STRIDE_TARGET_TRIPLE)
        list(APPEND COMPILER_ARGS "--target=${STRIDE_TARGET_TRIPLE}")
    elseif(CMAKE_CROSSCOMPILING)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "cortex-m0" OR CMAKE_SYSTEM_PROCESSOR MATCHES "armv6-m")
            list(APPEND COMPILER_ARGS "--target=thumbv6m-none-eabi")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "cortex-m3" OR CMAKE_SYSTEM_PROCESSOR MATCHES "armv7-m")
            list(APPEND COMPILER_ARGS "--target=thumbv7m-none-eabi")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "cortex-m4" OR CMAKE_SYSTEM_PROCESSOR MATCHES "cortex-m7")
            list(APPEND COMPILER_ARGS "--target=thumbv7em-none-eabi")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "cortex-m33" OR CMAKE_SYSTEM_PROCESSOR MATCHES "armv8-m")
            list(APPEND COMPILER_ARGS "--target=thumbv8m.main-none-eabi")
        elseif(CMAKE_SYSTEM_PROCESSOR)
            list(APPEND COMPILER_ARGS "--target=arm-none-eabi")
        endif()
    endif()

    if(STRIDE_TARGET_CPU)
        list(APPEND COMPILER_ARGS "-mcpu=${STRIDE_TARGET_CPU}")
    elseif(CMAKE_SYSTEM_PROCESSOR)
        list(APPEND COMPILER_ARGS "-mcpu=${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if(STRIDE_RELOC_MODEL)
        list(APPEND COMPILER_ARGS "-f${STRIDE_RELOC_MODEL}")
    endif()

    if(STRIDE_CODE_MODEL)
        list(APPEND COMPILER_ARGS "-mcmodel=${STRIDE_CODE_MODEL}")
    endif()

    set(GENERATED_OBJECTS "")

    foreach(STRIDE_SRC IN LISTS ARGN)
        get_filename_component(FILE_NAME "${STRIDE_SRC}" NAME_WE)
        get_filename_component(SRC_PATH "${STRIDE_SRC}" ABSOLUTE)

        set(OUT_OBJ "${GEN_DIR}/${FILE_NAME}${OBJ_EXT}")
        set(OUT_HDR "${GEN_INC_DIR}/${FILE_NAME}.h")

        add_custom_command(
            OUTPUT "${OUT_OBJ}" "${OUT_HDR}"
            COMMAND ${STRIDE_COMPILER_CMD}
                    -c "${SRC_PATH}"
                    -o "${OUT_OBJ}"
                    --emit-header="${OUT_HDR}"
                    ${COMPILER_ARGS}
            DEPENDS "${SRC_PATH}" ${STRIDE_COMPILER_DEP}
            COMMENT "Compiling Stride source ${STRIDE_SRC} -> ${FILE_NAME}${OBJ_EXT} / ${FILE_NAME}.h"
            VERBATIM
        )

        list(APPEND GENERATED_OBJECTS "${OUT_OBJ}")
    endforeach()

    # Link generated object files directly and expose generated header directory
    target_sources(${TARGET} ${VISIBILITY} ${GENERATED_OBJECTS})
    target_include_directories(${TARGET} ${VISIBILITY} "${GEN_INC_DIR}")
endfunction()

function(stride_add_library LIB_NAME)
    cmake_parse_arguments(ARG "STATIC;OBJECT" "" "" ${ARGN})
    set(LIB_TYPE "STATIC")
    if(ARG_OBJECT)
        set(LIB_TYPE "OBJECT")
    endif()

    set(SOURCES ${ARG_UNPARSED_ARGUMENTS})
    if(NOT SOURCES)
        message(FATAL_ERROR "stride_add_library requires at least one .stride source file.")
    endif()

    # Determine compiler command and dependency
    if(DEFINED STRIDE_COMPILER_BIN)
        set(STRIDE_COMPILER_CMD "${STRIDE_COMPILER_BIN}")
        set(STRIDE_COMPILER_DEP "${STRIDE_COMPILER_BIN}")
    elseif(TARGET Stride::Compiler)
        set(STRIDE_COMPILER_CMD Stride::Compiler)
        set(STRIDE_COMPILER_DEP Stride::Compiler)
    else()
        set(STRIDE_COMPILER_CMD stridec)
        set(STRIDE_COMPILER_DEP "")
    endif()

    set(OBJ_EXT "${CMAKE_C_OUTPUT_EXTENSION}")
    if(NOT OBJ_EXT)
        set(OBJ_EXT ".o")
    endif()

    set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/stride_gen_${LIB_NAME}")
    set(GEN_INC_DIR "${GEN_DIR}/include")
    file(MAKE_DIRECTORY "${GEN_INC_DIR}")

    set(GENERATED_OBJECTS "")
    set(GENERATED_HEADERS "")

    foreach(STRIDE_SRC IN LISTS SOURCES)
        get_filename_component(FILE_NAME "${STRIDE_SRC}" NAME_WE)
        get_filename_component(SRC_PATH "${STRIDE_SRC}" ABSOLUTE)

        set(OUT_OBJ "${GEN_DIR}/${FILE_NAME}${OBJ_EXT}")
        set(OUT_HDR "${GEN_INC_DIR}/${FILE_NAME}.h")

        add_custom_command(
            OUTPUT "${OUT_OBJ}" "${OUT_HDR}"
            COMMAND ${STRIDE_COMPILER_CMD}
                    -c "${SRC_PATH}"
                    -o "${OUT_OBJ}"
                    --emit-header="${OUT_HDR}"
            DEPENDS "${SRC_PATH}" ${STRIDE_COMPILER_DEP}
            COMMENT "Compiling Stride source ${STRIDE_SRC} -> ${FILE_NAME}${OBJ_EXT} / ${FILE_NAME}.h"
            VERBATIM
        )

        list(APPEND GENERATED_OBJECTS "${OUT_OBJ}")
        list(APPEND GENERATED_HEADERS "${OUT_HDR}")
    endforeach()

    add_library(${LIB_NAME} ${LIB_TYPE} ${GENERATED_OBJECTS} ${GENERATED_HEADERS})
    target_include_directories(${LIB_NAME} PUBLIC "${GEN_INC_DIR}")
endfunction()

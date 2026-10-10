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
    elseif(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR)
        list(APPEND COMPILER_ARGS "-mcpu=${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if(STRIDE_RELOC_MODEL)
        list(APPEND COMPILER_ARGS "-f${STRIDE_RELOC_MODEL}")
    endif()

    if(STRIDE_CODE_MODEL)
        list(APPEND COMPILER_ARGS "-mcmodel=${STRIDE_CODE_MODEL}")
    endif()

    if(STRIDE_EMIT_ALL_FUNCTIONS)
        list(APPEND COMPILER_ARGS "--emit-all-functions")
    endif()

    set(SRC_PATHS "")
    foreach(STRIDE_SRC IN LISTS ARGN)
        get_filename_component(SRC_PATH "${STRIDE_SRC}" ABSOLUTE)
        list(APPEND SRC_PATHS "${SRC_PATH}")
    endforeach()

    list(LENGTH ARGN NUM_SRCS)
    if(NUM_SRCS EQUAL 1)
        list(GET ARGN 0 FIRST_SRC)
        get_filename_component(SRC_BASE "${FIRST_SRC}" NAME_WE)
    else()
        set(SRC_BASE "${TARGET}")
    endif()

    set(OUT_OBJ "${GEN_DIR}/${TARGET}${OBJ_EXT}")
    set(OUT_HDR "${GEN_INC_DIR}/${SRC_BASE}.h")

    set(ALL_OUT_HDRS "${OUT_HDR}")
    set(POST_COMMANDS "")
    if(NOT "${SRC_BASE}" STREQUAL "${TARGET}")
        list(APPEND ALL_OUT_HDRS "${GEN_INC_DIR}/${TARGET}.h")
        set(POST_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUT_HDR}" "${GEN_INC_DIR}/${TARGET}.h")
    endif()

    add_custom_command(
        OUTPUT "${OUT_OBJ}" ${ALL_OUT_HDRS}
        COMMAND ${STRIDE_COMPILER_CMD}
                -c ${SRC_PATHS}
                -o "${OUT_OBJ}"
                --emit-header "${OUT_HDR}"
                ${COMPILER_ARGS}
        ${POST_COMMANDS}
        DEPENDS ${SRC_PATHS} ${STRIDE_COMPILER_DEP}
        COMMENT "Compiling Stride sources -> ${TARGET}${OBJ_EXT} / ${SRC_BASE}.h"
        VERBATIM
    )

    # Link generated unified object file directly and expose generated header directory
    target_sources(${TARGET} ${VISIBILITY} "${OUT_OBJ}")
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

    set(SRC_PATHS "")
    foreach(STRIDE_SRC IN LISTS SOURCES)
        get_filename_component(SRC_PATH "${STRIDE_SRC}" ABSOLUTE)
        list(APPEND SRC_PATHS "${SRC_PATH}")
    endforeach()

    set(OUT_OBJ "${GEN_DIR}/${LIB_NAME}${OBJ_EXT}")
    set(OUT_HDR "${GEN_INC_DIR}/${LIB_NAME}.h")

    add_custom_command(
        OUTPUT "${OUT_OBJ}" "${OUT_HDR}"
        COMMAND ${STRIDE_COMPILER_CMD}
                -c ${SRC_PATHS}
                -o "${OUT_OBJ}"
                --emit-header "${OUT_HDR}"
        DEPENDS ${SRC_PATHS} ${STRIDE_COMPILER_DEP}
        COMMENT "Compiling Stride library ${LIB_NAME} -> ${LIB_NAME}${OBJ_EXT} / ${LIB_NAME}.h"
        VERBATIM
    )

    add_library(${LIB_NAME} ${LIB_TYPE} "${OUT_OBJ}" "${OUT_HDR}")
    target_include_directories(${LIB_NAME} PUBLIC "${GEN_INC_DIR}")
endfunction()

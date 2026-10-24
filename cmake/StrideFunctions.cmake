#[=======================================================================[.rst:
StrideFunctions
---------------
Helper CMake functions for integrating Stride files into build targets.
#]=======================================================================]

macro(_stride_get_compiler_args OUT_VAR)
    set(${OUT_VAR} "")

    if(STRIDE_TARGET_TRIPLE)
        list(APPEND ${OUT_VAR} "--target=${STRIDE_TARGET_TRIPLE}")
    elseif(CMAKE_CROSSCOMPILING)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "cortex-m0" OR CMAKE_SYSTEM_PROCESSOR MATCHES "armv6-m")
            list(APPEND ${OUT_VAR} "--target=thumbv6m-none-eabi")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "cortex-m3" OR CMAKE_SYSTEM_PROCESSOR MATCHES "armv7-m")
            list(APPEND ${OUT_VAR} "--target=thumbv7m-none-eabi")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "cortex-m4" OR CMAKE_SYSTEM_PROCESSOR MATCHES "cortex-m7")
            list(APPEND ${OUT_VAR} "--target=thumbv7em-none-eabi")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "cortex-m33" OR CMAKE_SYSTEM_PROCESSOR MATCHES "armv8-m")
            list(APPEND ${OUT_VAR} "--target=thumbv8m.main-none-eabi")
        elseif(CMAKE_SYSTEM_PROCESSOR)
            list(APPEND ${OUT_VAR} "--target=arm-none-eabi")
        endif()
    endif()

    if(STRIDE_TARGET_CPU)
        list(APPEND ${OUT_VAR} "-mcpu=${STRIDE_TARGET_CPU}")
    elseif(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR)
        list(APPEND ${OUT_VAR} "-mcpu=${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if(STRIDE_RELOC_MODEL)
        list(APPEND ${OUT_VAR} "-f${STRIDE_RELOC_MODEL}")
    endif()

    if(STRIDE_CODE_MODEL)
        list(APPEND ${OUT_VAR} "-mcmodel=${STRIDE_CODE_MODEL}")
    endif()

    if(STRIDE_EMIT_ALL_FUNCTIONS)
        list(APPEND ${OUT_VAR} "--emit-all-functions")
    endif()

    # Pass configuration-dependent debug and optimization flags
    list(APPEND ${OUT_VAR}
        "$<$<CONFIG:Debug>:-g>"
        "$<$<CONFIG:Debug>:-O0>"
        "$<$<CONFIG:Debug>:--build-type=Debug>"
        "$<$<CONFIG:RelWithDebInfo>:-gline-tables-only>"
        "$<$<CONFIG:RelWithDebInfo>:-O2>"
        "$<$<CONFIG:RelWithDebInfo>:--build-type=RelWithDebInfo>"
        "$<$<CONFIG:Release>:-O3>"
        "$<$<CONFIG:Release>:--build-type=Release>"
        "$<$<CONFIG:MinSizeRel>:-Os>"
        "$<$<CONFIG:MinSizeRel>:--build-type=Release>"
    )

    if(STRIDE_COMPILER_ARGS)
        list(APPEND ${OUT_VAR} ${STRIDE_COMPILER_ARGS})
    endif()
    if(STRIDE_COPTS)
        list(APPEND ${OUT_VAR} ${STRIDE_COPTS})
    endif()
endmacro()

function(stride_target_sources TARGET VISIBILITY)
    cmake_parse_arguments(STRIDE_ARG "NO_DEFAULT_LIBS" "" "LINK_LIBRARIES;PLATFORM_LIBS" ${ARGN})
    set(SOURCES ${STRIDE_ARG_UNPARSED_ARGUMENTS})
    if(NOT SOURCES)
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

    _stride_get_compiler_args(COMPILER_ARGS)

    set(SRC_PATHS "")
    foreach(STRIDE_SRC IN LISTS SOURCES)
        get_filename_component(SRC_PATH "${STRIDE_SRC}" ABSOLUTE)
        list(APPEND SRC_PATHS "${SRC_PATH}")
    endforeach()

    list(LENGTH SOURCES NUM_SRCS)
    if(NUM_SRCS EQUAL 1)
        list(GET SOURCES 0 FIRST_SRC)
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

    # 1. Automatically link Stride Runtime if available
    if(TARGET Stride::Runtime)
        target_link_libraries(${TARGET} ${VISIBILITY} Stride::Runtime)
    elseif(TARGET StrideRuntime)
        target_link_libraries(${TARGET} ${VISIBILITY} StrideRuntime)
    endif()

    # 2. Automatically link platform/compiler C libraries unless NO_DEFAULT_LIBS is requested
    if(NOT STRIDE_ARG_NO_DEFAULT_LIBS)
        # Check if an embedded SDK target is active in the build tree
        set(_STRIDE_EMBEDDED_SDK FALSE)
        if(TARGET pico_stdlib OR TARGET pico_base OR TARGET zephyr_interface OR TARGET idf::main OR DEFINED ZEPHYR_BASE OR DEFINED IDF_PATH)
            set(_STRIDE_EMBEDDED_SDK TRUE)
        endif()

        # Check for bare-metal generic toolchain
        set(_STRIDE_BAREMETAL FALSE)
        if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
            set(_STRIDE_BAREMETAL TRUE)
        endif()

        # On standard desktop OS (Linux/macOS), link standard math library (libm) on non-MSVC
        if(NOT _STRIDE_EMBEDDED_SDK AND NOT _STRIDE_BAREMETAL)
            if(NOT MSVC)
                find_library(_STRIDE_LIBM m)
                if(_STRIDE_LIBM)
                    target_link_libraries(${TARGET} ${VISIBILITY} ${_STRIDE_LIBM})
                else()
                    target_link_libraries(${TARGET} ${VISIBILITY} m)
                endif()
            endif()
        endif()
    endif()

    # 3. Link explicit libraries if provided via arguments or target property
    if(STRIDE_ARG_LINK_LIBRARIES)
        target_link_libraries(${TARGET} ${VISIBILITY} ${STRIDE_ARG_LINK_LIBRARIES})
    endif()
    if(STRIDE_ARG_PLATFORM_LIBS)
        target_link_libraries(${TARGET} ${VISIBILITY} ${STRIDE_ARG_PLATFORM_LIBS})
    endif()

    get_target_property(_TARGET_STRIDE_LIBS ${TARGET} STRIDE_LINK_LIBRARIES)
    if(_TARGET_STRIDE_LIBS)
        target_link_libraries(${TARGET} ${VISIBILITY} ${_TARGET_STRIDE_LIBS})
    endif()
endfunction()

function(stride_add_arduino_library LIB_NAME)
    cmake_parse_arguments(ARG "" "OUTPUT_DIR;AUTHOR;VERSION;ARCH;DOMAIN" "SOURCES;MCUS" ${ARGN})
    set(SOURCES ${ARG_SOURCES} ${ARG_UNPARSED_ARGUMENTS})
    if(NOT SOURCES)
        message(FATAL_ERROR "stride_add_arduino_library requires at least one .stride source file.")
    endif()

    if(TARGET Stride::Compiler)
        set(STRIDE_COMPILER_CMD Stride::Compiler)
        set(STRIDE_COMPILER_DEP Stride::Compiler)
    elseif(STRIDE_COMPILER_BIN)
        set(STRIDE_COMPILER_CMD "${STRIDE_COMPILER_BIN}")
        set(STRIDE_COMPILER_DEP "${STRIDE_COMPILER_BIN}")
    else()
        find_program(STRIDE_COMPILER_CMD stridec)
        if(NOT STRIDE_COMPILER_CMD)
            message(FATAL_ERROR "stridec compiler executable not found. Ensure Stride is installed or STRIDE_COMPILER_BIN is set.")
        endif()
        set(STRIDE_COMPILER_DEP "")
    endif()

    if(NOT ARG_OUTPUT_DIR)
        set(ARG_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}")
    endif()

    set(SRC_PATHS "")
    foreach(STRIDE_SRC IN LISTS SOURCES)
        get_filename_component(SRC_PATH "${STRIDE_SRC}" ABSOLUTE)
        list(APPEND SRC_PATHS "${SRC_PATH}")
    endforeach()

    set(EXTRA_ARGS "")
    if(ARG_AUTHOR)
        list(APPEND EXTRA_ARGS "--arduino-author=${ARG_AUTHOR}")
    endif()
    if(ARG_VERSION)
        list(APPEND EXTRA_ARGS "--arduino-version=${ARG_VERSION}")
    endif()
    if(ARG_ARCH)
        list(APPEND EXTRA_ARGS "--arduino-arch=${ARG_ARCH}")
    endif()
    if(ARG_DOMAIN)
        list(APPEND EXTRA_ARGS "--domain=${ARG_DOMAIN}")
    endif()
    if(ARG_MCUS)
        string(REPLACE ";" "," MCU_LIST "${ARG_MCUS}")
        list(APPEND EXTRA_ARGS "--arduino-mcu=${MCU_LIST}")
    endif()

    add_custom_target(${LIB_NAME}
        COMMAND ${STRIDE_COMPILER_CMD}
            --emit-arduino-lib="${ARG_OUTPUT_DIR}"
            --arduino-name="${LIB_NAME}"
            ${EXTRA_ARGS}
            ${SRC_PATHS}
        DEPENDS ${SRC_PATHS} ${STRIDE_COMPILER_DEP}
        COMMENT "Generating Arduino pre-built library bundle: ${LIB_NAME} in ${ARG_OUTPUT_DIR}"
        VERBATIM
    )
endfunction()

function(stride_add_library LIB_NAME)
    cmake_parse_arguments(ARG "STATIC;OBJECT;ARDUINO;NO_DEFAULT_LIBS" "OUTPUT_DIR;AUTHOR;VERSION;ARCH;DOMAIN" "LINK_LIBRARIES;PLATFORM_LIBS;MCUS" ${ARGN})
    if(ARG_ARDUINO)
        stride_add_arduino_library(${LIB_NAME} ${ARGN})
        return()
    endif()

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

    _stride_get_compiler_args(COMPILER_ARGS)

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
                ${COMPILER_ARGS}
        DEPENDS ${SRC_PATHS} ${STRIDE_COMPILER_DEP}
        COMMENT "Compiling Stride library ${LIB_NAME} -> ${LIB_NAME}${OBJ_EXT} / ${LIB_NAME}.h"
        VERBATIM
    )

    add_library(${LIB_NAME} ${LIB_TYPE} "${OUT_OBJ}" "${OUT_HDR}")
    target_include_directories(${LIB_NAME} PUBLIC "${GEN_INC_DIR}")

    set(LINK_VIS "PUBLIC")
    if(LIB_TYPE STREQUAL "OBJECT")
        set(LINK_VIS "PRIVATE")
    endif()

    # 1. Link Stride Runtime if available
    if(TARGET Stride::Runtime)
        target_link_libraries(${LIB_NAME} ${LINK_VIS} Stride::Runtime)
    elseif(TARGET StrideRuntime)
        target_link_libraries(${LIB_NAME} ${LINK_VIS} StrideRuntime)
    endif()

    # 2. Automatically link platform/compiler C libraries unless NO_DEFAULT_LIBS is requested
    if(NOT ARG_NO_DEFAULT_LIBS)
        set(_STRIDE_EMBEDDED_SDK FALSE)
        if(TARGET pico_stdlib OR TARGET pico_base OR TARGET zephyr_interface OR TARGET idf::main OR DEFINED ZEPHYR_BASE OR DEFINED IDF_PATH)
            set(_STRIDE_EMBEDDED_SDK TRUE)
        endif()

        set(_STRIDE_BAREMETAL FALSE)
        if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
            set(_STRIDE_BAREMETAL TRUE)
        endif()

        if(NOT _STRIDE_EMBEDDED_SDK AND NOT _STRIDE_BAREMETAL)
            if(NOT MSVC)
                find_library(_STRIDE_LIBM m)
                if(_STRIDE_LIBM)
                    target_link_libraries(${LIB_NAME} ${LINK_VIS} ${_STRIDE_LIBM})
                else()
                    target_link_libraries(${LIB_NAME} ${LINK_VIS} m)
                endif()
            endif()
        endif()
    endif()

    # 3. Link explicit libraries if provided via arguments or property
    if(ARG_LINK_LIBRARIES)
        target_link_libraries(${LIB_NAME} ${LINK_VIS} ${ARG_LINK_LIBRARIES})
    endif()
    if(ARG_PLATFORM_LIBS)
        target_link_libraries(${LIB_NAME} ${LINK_VIS} ${ARG_PLATFORM_LIBS})
    endif()

    get_target_property(_LIB_STRIDE_LIBS ${LIB_NAME} STRIDE_LINK_LIBRARIES)
    if(_LIB_STRIDE_LIBS)
        target_link_libraries(${LIB_NAME} ${LINK_VIS} ${_LIB_STRIDE_LIBS})
    endif()
endfunction()

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    message("Using PICO_SDK_PATH from environment ('${PICO_SDK_PATH}')")
endif ()

if (DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK")
endif ()

if (NOT PICO_SDK_PATH)
    message(WARNING "PICO_SDK_PATH is not set. To build this firmware, set PICO_SDK_PATH or install the Raspberry Pi Pico SDK.")
else ()
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake OPTIONAL)
endif ()

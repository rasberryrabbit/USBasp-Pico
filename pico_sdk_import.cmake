# This is a copy of pico_sdk_import.cmake from the pico-sdk, which can be
# found at:
#   https://github.com/raspberrypi/pico-sdk/blob/develop/external/pico_sdk_import.cmake
#
# It is included by the project's CMakeLists.txt before project() so that
# the SDK is pulled in and initialized correctly.
#
# The PICO_SDK_PATH environment variable or CMake cache entry must point
# to your local clone of the pico-sdk.

# This can be dropped into an external project to help locate the pico-sdk
# It should be include()ed before your project() command.

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    message("Using PICO_SDK_PATH from environment ('${PICO_SDK_PATH}')")
endif ()

if (DEFINED ENV{PICO_SDK_FETCH_FROM_GIT} AND (NOT PICO_SDK_FETCH_FROM_GIT))
    set(PICO_SDK_FETCH_FROM_GIT $ENV{PICO_SDK_FETCH_FROM_GIT})
    message("Using PICO_SDK_FETCH_FROM_GIT from environment ('${PICO_SDK_FETCH_FROM_GIT}')")
endif ()

if (DEFINED ENV{PICO_SDK_FETCH_FROM_GIT_PATH} AND (NOT PICO_SDK_FETCH_FROM_GIT_PATH))
    set(PICO_SDK_FETCH_FROM_GIT_PATH $ENV{PICO_SDK_FETCH_FROM_GIT_PATH})
    message("Using PICO_SDK_FETCH_FROM_GIT_PATH from environment ('${PICO_SDK_FETCH_FROM_GIT_PATH}')")
endif ()

set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK")
set(PICO_SDK_FETCH_FROM_GIT "${PICO_SDK_FETCH_FROM_GIT}" CACHE BOOL "Set to ON to fetch pico-sdk from git if not otherwise locatable")
set(PICO_SDK_FETCH_FROM_GIT_PATH "${PICO_SDK_FETCH_FROM_GIT_PATH}" CACHE FILEPATH "location to download ignored (RPIP build)")

if (NOT PICO_SDK_PATH)
    if(PICO_SDK_FETCH_FROM_GIT)
        include(FetchContent)
        set(FETCHCONTENT_BASE_DIR ${CMAKE_BINARY_DIR}/_deps)
        if (NOT PICO_SDK_FETCH_FROM_GIT_PATH)
            set(PICO_SDK_FETCH_FROM_GIT_PATH ${FETCHCONTENT_BASE_DIR}/pico-sdk)
        endif ()
        FetchContent_Declare(
                pico_sdk
                GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk
                GIT_TAG sdk-1.5.1
                GIT_SHALLOW FALSE
        )
        FetchContent_MakeAvailable(pico_sdk)
        set(PICO_SDK_PATH ${pico_sdk_SOURCE_DIR})
    else ()
        message(FATAL_ERROR
                "PICO_SDK_PATH not defined and PICO_SDK_FETCH_FROM_GIT is OFF.\n"
                "Either set PICO_SDK_PATH to the path of your pico-sdk clone,\n"
                "or set PICO_SDK_FETCH_FROM_GIT=ON to download it automatically.")
    endif ()
endif ()

get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
if (NOT EXISTS ${PICO_SDK_PATH}/pico_sdk_init.cmake)
    message(FATAL_ERROR "Directory '${PICO_SDK_PATH}' does not appear to contain the Raspberry Pi Pico SDK.\n"
            "Expected to find '${PICO_SDK_PATH}/pico_sdk_init.cmake'")
endif ()

set(PICO_SDK_INIT_CMAKE_FILE ${PICO_SDK_PATH}/pico_sdk_init.cmake CACHE FILEPATH "Path to pico_sdk_init.cmake")
include(${PICO_SDK_INIT_CMAKE_FILE})

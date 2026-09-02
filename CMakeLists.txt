cmake_minimum_required(VERSION 3.21)

project(AOWIS-SERVER-EPANET LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(AOWIS_SERVER_EPANET_BUILD_SERVER "Build AOWIS EPANET REST server executable" ON)
option(AOWIS_SERVER_EPANET_BUILD_SHARED_EPANET "Build EPANET as shared library" ON)
option(AOWIS_SERVER_EPANET_FETCH_MODEL "Fetch AOWIS-SERVER-MODEL when it is not provided by a parent project" ${PROJECT_IS_TOP_LEVEL})
option(AOWIS_SERVER_EPANET_BUILD_TESTS "Build AOWIS EPANET adapter integration tests" ${PROJECT_IS_TOP_LEVEL})
option(AOWIS_SERVER_EPANET_ENABLE_UPSTREAM_BOOST_TESTS "Expose the original vendored EPANET Boost suite through the AOWIS CTest harness" OFF)
option(AOWIS_SERVER_EPANET_ENABLE_COVERAGE "Instrument the AOWIS EPANET adapter and expose line/branch coverage reporting" OFF)

set(AOWIS_SERVER_MODEL_SOURCE_DIR "" CACHE PATH "Optional local AOWIS-SERVER-MODEL source directory")
set(AOWIS_SERVER_MODEL_GIT_REPOSITORY "https://github.com/aowis-org/AOWIS-SERVER-MODEL.git" CACHE STRING "Git repository used to fetch AOWIS-SERVER-MODEL")
set(AOWIS_SERVER_MODEL_GIT_TAG "main" CACHE STRING "Git branch, tag, or commit used to fetch AOWIS-SERVER-MODEL")

find_package(Qt6 REQUIRED COMPONENTS Core)

if(AOWIS_SERVER_EPANET_BUILD_SERVER)
    find_package(Qt6 REQUIRED COMPONENTS Network HttpServer)
endif()

if(NOT TARGET AOWIS::ServerModel)
    if(AOWIS_SERVER_MODEL_SOURCE_DIR)
        get_filename_component(
            AOWIS_SERVER_MODEL_SOURCE_DIR_ABSOLUTE
            "${AOWIS_SERVER_MODEL_SOURCE_DIR}"
            ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
        )

        if(NOT EXISTS "${AOWIS_SERVER_MODEL_SOURCE_DIR_ABSOLUTE}/CMakeLists.txt")
            message(FATAL_ERROR
                "AOWIS_SERVER_MODEL_SOURCE_DIR does not contain a CMake project: "
                "${AOWIS_SERVER_MODEL_SOURCE_DIR_ABSOLUTE}"
            )
        endif()

        add_subdirectory(
            "${AOWIS_SERVER_MODEL_SOURCE_DIR_ABSOLUTE}"
            "${CMAKE_CURRENT_BINARY_DIR}/_deps/aowis-server-model-build"
            EXCLUDE_FROM_ALL
        )
    elseif(AOWIS_SERVER_EPANET_FETCH_MODEL)
        include(FetchContent)

        FetchContent_Declare(
            aowis_server_model
            GIT_REPOSITORY "${AOWIS_SERVER_MODEL_GIT_REPOSITORY}"
            GIT_TAG "${AOWIS_SERVER_MODEL_GIT_TAG}"
            GIT_PROGRESS TRUE
        )

        FetchContent_MakeAvailable(aowis_server_model)
    else()
        message(FATAL_ERROR
            "AOWIS::ServerModel is unavailable. Provide it from the parent project, "
            "set AOWIS_SERVER_MODEL_SOURCE_DIR, or enable "
            "AOWIS_SERVER_EPANET_FETCH_MODEL."
        )
    endif()
endif()

if(NOT TARGET AOWIS::ServerModel)
    message(FATAL_ERROR
        "The AOWIS-SERVER-MODEL project did not create AOWIS::ServerModel."
    )
endif()

if(TARGET GeographicLib_STATIC)
    # When embedded in AOWIS-SERVER-GUI, reuse the GeographicLib target that
    # the parent project already provides.
    set(AOWIS_SERVER_EPANET_GEOGRAPHICLIB_TARGET GeographicLib_STATIC)
else()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/GeographicLib/include/GeographicLib/LocalCartesian.hpp")
        set(AOWIS_SERVER_EPANET_GEOGRAPHICLIB_SOURCE_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/external/GeographicLib")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../GeographicLib/include/GeographicLib/LocalCartesian.hpp")
        # Standalone adapter builds inside the GUI checkout can use the GUI's
        # GeographicLib source tree even when the adapter-local submodule has
        # not been initialized yet.
        set(AOWIS_SERVER_EPANET_GEOGRAPHICLIB_SOURCE_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/../GeographicLib")
    else()
        message(FATAL_ERROR
            "GeographicLib is required for INP geometry import. Add the adapter-local "
            "submodule at external/GeographicLib (GeographicLib 2.7 / r2.7), or build "
            "the adapter from the GUI checkout where ../GeographicLib is available."
        )
    endif()

    # GeographicLib 2.7 unconditionally registers its own large CTest suite
    # from its top-level CMakeLists.txt. The adapter only needs the coordinate
    # conversion primitives used by INP geometry import,
    # so build that dependency directly instead of polluting the AOWIS test
    # registry with GeographicLib tests and tools.
    include(TestBigEndian)
    test_big_endian(AOWIS_SERVER_EPANET_GEOGRAPHICLIB_BIG_ENDIAN)
    if(AOWIS_SERVER_EPANET_GEOGRAPHICLIB_BIG_ENDIAN)
        set(AOWIS_SERVER_EPANET_GEOGRAPHICLIB_BIG_ENDIAN_VALUE 1)
    else()
        set(AOWIS_SERVER_EPANET_GEOGRAPHICLIB_BIG_ENDIAN_VALUE 0)
    endif()

    set(AOWIS_SERVER_EPANET_GEOGRAPHICLIB_GENERATED_INCLUDE_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/_deps/geographiclib-minimal/include")
    file(MAKE_DIRECTORY
        "${AOWIS_SERVER_EPANET_GEOGRAPHICLIB_GENERATED_INCLUDE_DIR}/GeographicLib")
    file(WRITE
        "${AOWIS_SERVER_EPANET_GEOGRAPHICLIB_GENERATED_INCLUDE_DIR}/GeographicLib/Config.h"
        "#define GEOGRAPHICLIB_VERSION_STRING \"2.7.0\"\n"
        "#define GEOGRAPHICLIB_VERSION_MAJOR 2\n"
        "#define GEOGRAPHICLIB_VERSION_MINOR 7\n"
        "#define GEOGRAPHICLIB_VERSION_PATCH 0\n"
        "#define GEOGRAPHICLIB_DATA \"\"\n"
        "#define GEOGRAPHICLIB_HAVE_LONG_DOUBLE 0\n"
        "#define GEOGRAPHICLIB_WORDS_BIGENDIAN ${AOWIS_SERVER_EPANET_GEOGRAPHICLIB_BIG_ENDIAN_VALUE}\n"
        "#define GEOGRAPHICLIB_PRECISION 2\n"
        "#define GEOGRAPHICLIB_SHARED_LIB 0\n"
    )

    add_library(aowis-server-epanet-geographiclib STATIC
        "${AOWIS_SERVER_EPANET_GEOGRAPHICLIB_SOURCE_DIR}/src/Math.cpp"
        "${AOWIS_SERVER_EPANET_GEOGRAPHICLIB_SOURCE_DIR}/src/Geocentric.cpp"
        "${AOWIS_SERVER_EPANET_GEOGRAPHICLIB_SOURCE_DIR}/src/LocalCartesian.cpp"
        "${AOWIS_SERVER_EPANET_GEOGRAPHICLIB_SOURCE_DIR}/src/LambertConformalConic.cpp"
    )
    target_include_directories(aowis-server-epanet-geographiclib
        PUBLIC
            "${AOWIS_SERVER_EPANET_GEOGRAPHICLIB_GENERATED_INCLUDE_DIR}"
            "${AOWIS_SERVER_EPANET_GEOGRAPHICLIB_SOURCE_DIR}/include"
    )
    target_compile_definitions(aowis-server-epanet-geographiclib
        PUBLIC GEOGRAPHICLIB_SHARED_LIB=0)
    target_compile_features(aowis-server-epanet-geographiclib PUBLIC cxx_std_20)
    set_target_properties(aowis-server-epanet-geographiclib PROPERTIES
        POSITION_INDEPENDENT_CODE ON)

    set(AOWIS_SERVER_EPANET_GEOGRAPHICLIB_TARGET
        aowis-server-epanet-geographiclib)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/epanet/CMakeLists.txt")
    message(FATAL_ERROR
        "EPANET submodule is missing. Run: git submodule update --init --recursive"
    )
endif()

set(BUILD_TESTS OFF CACHE BOOL "Build EPANET tests" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "Build EPANET docs" FORCE)
set(BUILD_COVERAGE OFF CACHE BOOL "Build EPANET coverage" FORCE)
set(BUILD_SHARED_LIBS ${AOWIS_SERVER_EPANET_BUILD_SHARED_EPANET} CACHE BOOL "Build shared libraries" FORCE)

add_subdirectory(external/epanet EXCLUDE_FROM_ALL)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/epanet-msx/src/solver/include/epanetmsx.h")
    message(FATAL_ERROR
        "EPANET-MSX submodule is missing. Run: git submodule update --init --recursive"
    )
endif()

# Build only the USEPA EPANET-MSX solver and bind it to the already-created
# AOWIS EPANET target. Do not configure the EPANET 2.2 copy bundled by MSX.
include(cmake/aowis_epanet_msx.cmake)

if(WIN32 AND NOT AOWIS_SERVER_EPANET_BUILD_SHARED_EPANET)
    target_compile_definitions(epanet2 PUBLIC "DLLEXPORT=__stdcall")
endif()

if(TARGET epanet2 AND UNIX AND NOT APPLE)
    target_link_libraries(epanet2 PUBLIC m)
endif()

# Deliberately include this file instead of add_subdirectory(src/lib).
# This causes the library target to belong to the repository root, so Qt Creator
# treats both include/ and src/ as normal project paths instead of Other Locations.
include(src/lib/CMakeLists.txt)

if(AOWIS_SERVER_EPANET_ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR
            "AOWIS_SERVER_EPANET_ENABLE_COVERAGE currently requires GCC."
        )
    endif()

    find_program(AOWIS_SERVER_EPANET_GCOVR_EXECUTABLE gcovr)
    if(NOT AOWIS_SERVER_EPANET_GCOVR_EXECUTABLE)
        message(FATAL_ERROR
            "AOWIS_SERVER_EPANET_ENABLE_COVERAGE requires gcovr in PATH."
        )
    endif()

    target_compile_options(aowis-server-epanet PRIVATE
        -O0
        -g
        --coverage
    )
    target_link_options(aowis-server-epanet INTERFACE --coverage)
endif()

if(AOWIS_SERVER_EPANET_BUILD_SERVER)
    add_subdirectory(src/server)
endif()

if(AOWIS_SERVER_EPANET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

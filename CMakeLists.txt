cmake_minimum_required(VERSION 3.21)

project(AOWIS-SERVER-EPANET LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(
    AOWIS_SERVER_EPANET_BUILD_SERVER
    "Build AOWIS EPANET REST server executable"
    ON
)

option(
    AOWIS_SERVER_EPANET_BUILD_SHARED_EPANET
    "Build EPANET as shared library"
    ON
)

option(
    AOWIS_SERVER_EPANET_FETCH_MODEL
    "Fetch AOWIS-SERVER-MODEL when it is not provided by a parent project"
    ${PROJECT_IS_TOP_LEVEL}
)

set(
    AOWIS_SERVER_MODEL_SOURCE_DIR
    ""
    CACHE PATH
    "Optional local AOWIS-SERVER-MODEL source directory"
)

set(
    AOWIS_SERVER_MODEL_GIT_REPOSITORY
    "https://github.com/aowis-org/AOWIS-SERVER-MODEL.git"
    CACHE STRING
    "Git repository used to fetch AOWIS-SERVER-MODEL"
)

set(
    AOWIS_SERVER_MODEL_GIT_TAG
    "main"
    CACHE STRING
    "Git branch, tag, or commit used to fetch AOWIS-SERVER-MODEL"
)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

if(AOWIS_SERVER_EPANET_BUILD_SERVER)
    find_package(Qt6 REQUIRED COMPONENTS HttpServer)
endif()

#
# AOWIS-SERVER-MODEL
#
# Dependency resolution order:
#
# 1. Use AOWIS::ServerModel when a parent project already provided it.
# 2. Use AOWIS_SERVER_MODEL_SOURCE_DIR when explicitly configured.
# 3. Fetch the repository when standalone fetching is enabled.
#

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
                "AOWIS_SERVER_MODEL_SOURCE_DIR does not contain a CMake project:\n"
                "  ${AOWIS_SERVER_MODEL_SOURCE_DIR_ABSOLUTE}"
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
            "AOWIS::ServerModel is unavailable.\n"
            "\n"
            "When AOWIS-SERVER-EPANET is included by another project, add\n"
            "AOWIS-SERVER-MODEL before AOWIS-SERVER-EPANET.\n"
            "\n"
            "For a standalone build, either enable:\n"
            "  -DAOWIS_SERVER_EPANET_FETCH_MODEL=ON\n"
            "\n"
            "or provide a local checkout:\n"
            "  -DAOWIS_SERVER_MODEL_SOURCE_DIR=/path/to/AOWIS-SERVER-MODEL"
        )
    endif()
endif()

if(NOT TARGET AOWIS::ServerModel)
    message(FATAL_ERROR
        "The AOWIS-SERVER-MODEL project was loaded, but it did not create "
        "the required AOWIS::ServerModel target."
    )
endif()

#
# EPANET
#

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/epanet/CMakeLists.txt")
    message(FATAL_ERROR
        "EPANET submodule is missing. Run:\n"
        "  git submodule update --init --recursive"
    )
endif()

set(BUILD_TESTS OFF CACHE BOOL "Build EPANET tests" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "Build EPANET docs" FORCE)
set(BUILD_COVERAGE OFF CACHE BOOL "Build EPANET coverage" FORCE)

set(
    BUILD_SHARED_LIBS
    ${AOWIS_SERVER_EPANET_BUILD_SHARED_EPANET}
    CACHE BOOL
    "Build shared libraries"
    FORCE
)

add_subdirectory(external/epanet EXCLUDE_FROM_ALL)

if(WIN32 AND NOT AOWIS_SERVER_EPANET_BUILD_SHARED_EPANET)
    target_compile_definitions(epanet2
        PUBLIC
            "DLLEXPORT=__stdcall"
    )
endif()

if(TARGET epanet2 AND UNIX AND NOT APPLE)
    target_link_libraries(epanet2 PUBLIC m)
endif()

#
# AOWIS EPANET library and server
#

add_subdirectory(src/lib)

if(AOWIS_SERVER_EPANET_BUILD_SERVER)
    add_subdirectory(src/server)
endif()

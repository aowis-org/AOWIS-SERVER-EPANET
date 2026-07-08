cmake_minimum_required(VERSION 3.16)

project(AOWIS-SERVER-EPANET LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(AOWIS_SERVER_EPANET_BUILD_SERVER "Build AOWIS EPANET REST server executable" ON)
option(AOWIS_SERVER_EPANET_BUILD_SHARED_EPANET "Build EPANET as shared library" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

if(AOWIS_SERVER_EPANET_BUILD_SERVER)
    find_package(Qt6 REQUIRED COMPONENTS HttpServer)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/epanet/CMakeLists.txt")
    message(FATAL_ERROR
        "EPANET submodule is missing. Run:\n"
        "  git submodule update --init --recursive"
    )
endif()

set(BUILD_TESTS OFF CACHE BOOL "Build EPANET tests" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "Build EPANET docs" FORCE)
set(BUILD_COVERAGE OFF CACHE BOOL "Build EPANET coverage" FORCE)

set(BUILD_SHARED_LIBS
    ${AOWIS_SERVER_EPANET_BUILD_SHARED_EPANET}
    CACHE BOOL
    "Build shared libraries"
    FORCE
)

add_subdirectory(external/epanet EXCLUDE_FROM_ALL)

if(TARGET epanet2 AND UNIX AND NOT APPLE)
    target_link_libraries(epanet2 PUBLIC m)
endif()

add_subdirectory(src/lib)

if(AOWIS_SERVER_EPANET_BUILD_SERVER)
    add_subdirectory(src/server)
endif()


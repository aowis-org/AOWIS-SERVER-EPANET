# Build the USEPA EPANET-MSX solver against AOWIS's existing EPANET target.
#
# The upstream EPANET-MSX project normally configures its bundled EPANET 2.2
# and command-line runner. AOWIS needs only the MSX solver library, and it must
# use the same EPANET implementation as the rest of the adapter.

if(NOT TARGET epanet2)
    message(FATAL_ERROR
        "EPANET-MSX requires the AOWIS epanet2 target to be created first."
    )
endif()

set(AOWIS_SERVER_EPANET_MSX_SOLVER_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../external/epanet-msx/src/solver"
)

# Keep the vendored solver input set explicit. Using CONFIGURE_DEPENDS here
# makes Ninja re-run CMake whenever its glob verification sees the external
# source directory change, which is especially fragile when this adapter is
# itself built as a submodule of another project. The EPANET-MSX revision is
# pinned, so additions/removals should be reviewed deliberately.
set(AOWIS_SERVER_EPANET_MSX_SOURCES
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/hash.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/mathexpr.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/mempool.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxchem.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxcompiler.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxdispersion.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxerr.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxfile.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxfuncs.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxinp.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxout.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxproj.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxqual.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxrpt.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxtank.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxtoolkit.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxutils.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/newton.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/rk5.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/ros2.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/smatrix.c"
    "${CMAKE_CURRENT_LIST_DIR}/../src/lib/internal/epanet_msx_mass_balance_bridge.c"
)

add_library(epanetmsx ${AOWIS_SERVER_EPANET_MSX_SOURCES})

target_include_directories(epanetmsx
    PUBLIC
        "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/include"
    PRIVATE
        "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}"
)

target_link_libraries(epanetmsx
    PUBLIC
        epanet2
)

if(NOT MSVC)
    target_link_libraries(epanetmsx PUBLIC m)
endif()

find_package(OpenMP COMPONENTS C)
if(OpenMP_C_FOUND)
    target_compile_definitions(epanetmsx PRIVATE USE_OPENMP)
    target_link_libraries(epanetmsx PUBLIC OpenMP::OpenMP_C)
endif()

if(WIN32 AND NOT BUILD_SHARED_LIBS)
    # epanetmsx.h assumes Windows consumers use a DLL unless MSXDLLEXPORT is
    # supplied. Static AOWIS builds still need the toolkit's stdcall ABI but
    # must not mark its symbols as dllimport.
    target_compile_definitions(epanetmsx PUBLIC "MSXDLLEXPORT=__stdcall")
endif()

set_target_properties(epanetmsx PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

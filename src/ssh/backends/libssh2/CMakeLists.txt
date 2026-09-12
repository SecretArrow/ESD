# ---------------------------------------------------------------------------
# eclipse_ssh2backend - libssh2 (BSD-3) backend for Eclipse SSH Desktop.
#
# Consumes the PkgConfig::LIBSSH2 imported target defined by the top-level
# CMakeLists (guarded by ECLIPSE_WITH_LIBSSH2) and the eclipse_core library
# (logging, utils) from the parent src/ directory.
# ---------------------------------------------------------------------------

add_library(eclipse_ssh2backend STATIC
    Libssh2Engine.cpp
)

target_include_directories(eclipse_ssh2backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(eclipse_ssh2backend
    PUBLIC
        Qt6::Core
        Qt6::Network
        PkgConfig::LIBSSH2
        eclipse_core
)

eclipse_set_target_warnings(eclipse_ssh2backend)

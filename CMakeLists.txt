cmake_minimum_required(VERSION 3.24)

project(EclipseSSH
    VERSION 0.1.0
    DESCRIPTION "Eclipse SSH Desktop - a modern, modular SSH/SFTP client"
    LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(ECLIPSE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ECLIPSE_BUILD_TESTS        "Build automated tests"            ON)
option(ECLIPSE_WITH_LIBSSH        "Enable libssh SSH backend"        ON)
option(ECLIPSE_WITH_LIBSSH2       "Enable libssh2 SSH backend"       ON)
option(ECLIPSE_DEVELOPER_MODE     "Enable developer diagnostics UI"  ON)

include(CompilerWarnings)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Sql Network Concurrent Widgets)
qt_standard_project_setup()

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

if(ECLIPSE_WITH_LIBSSH)
    pkg_check_modules(LIBSSH IMPORTED_TARGET REQUIRED libssh)
    message(STATUS "libssh backend enabled: ${LIBSSH_VERSION}")
endif()
if(ECLIPSE_WITH_LIBSSH2)
    pkg_check_modules(LIBSSH2 IMPORTED_TARGET REQUIRED libssh2)
    message(STATUS "libssh2 backend enabled: ${LIBSSH2_VERSION}")
endif()
pkg_check_modules(VTERM IMPORTED_TARGET REQUIRED vterm)
message(STATUS "libvterm: ${VTERM_VERSION}")

configure_file(cmake/EclipseVersion.h.in ${CMAKE_BINARY_DIR}/generated/EclipseVersion.h @ONLY)

add_subdirectory(src)

if(ECLIPSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

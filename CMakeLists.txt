cmake_minimum_required(VERSION 3.24)
project(eclipse-ssh VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(ECLIPSE_BUILD_UI "Build the GTK4 GUI" ON)
option(ECLIPSE_BUILD_TESTS "Build tests" ON)
option(ECLIPSE_WERROR "Treat warnings as errors (spec rule 20)" ON)
option(ECLIPSE_ASAN "Enable ASan/UBSan" OFF)

# Local-sandbox static libssh needs zlib+krb appended after targets; CI uses
# shared libssh where this is unnecessary (and libkrb5 may be absent).
if(ECLIPSE_EXTRA_INCLUDES)
  set(CMAKE_C_STANDARD_LIBRARIES "-lz -lgssapi_krb5 -lkrb5 -lk5crypto -lcom_err ${CMAKE_C_STANDARD_LIBRARIES}")
endif()

if(ECLIPSE_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(PkgConfig REQUIRED)
if(NOT WIN32)
  pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0)
  add_library(ecdep::GLIB2 ALIAS PkgConfig::GLIB2)
  pkg_check_modules(GIO2 REQUIRED IMPORTED_TARGET gio-2.0)
  add_library(ecdep::GIO2 ALIAS PkgConfig::GIO2)
  pkg_check_modules(LIBSSH REQUIRED IMPORTED_TARGET libssh)
  add_library(ecdep::LIBSSH ALIAS PkgConfig::LIBSSH)
  pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)
  add_library(ecdep::VTERM ALIAS PkgConfig::VTERM)
  if(ECLIPSE_BUILD_UI)
    pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
    add_library(ecdep::GTK4 ALIAS PkgConfig::GTK4)
  endif()
endif()
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
# ---- shared compile hygiene for our own sources ----
add_library(ec_flags INTERFACE)
target_compile_options(ec_flags INTERFACE
  -Wall -Wextra -Wshadow -Wconversion -Wno-unused-parameter -Wno-missing-field-initializers
  $<$<BOOL:${ECLIPSE_WERROR}>:-Werror>
  -D_GNU_SOURCE -D_DEFAULT_SOURCE)
if(WIN32)
  target_compile_definitions(ec_flags INTERFACE _WIN32_WINNT=0x0601)
endif()

# ---- umbrella include path for all modules ----
add_library(ec_includes INTERFACE)
target_include_directories(ec_includes INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/core
  ${CMAKE_CURRENT_SOURCE_DIR}/platform
  ${CMAKE_CURRENT_SOURCE_DIR}/security
  ${CMAKE_CURRENT_SOURCE_DIR}/ssh
  ${CMAKE_CURRENT_SOURCE_DIR}/sftp
  ${CMAKE_CURRENT_SOURCE_DIR}/terminal
  ${CMAKE_CURRENT_SOURCE_DIR}/forwarding
  ${CMAKE_CURRENT_SOURCE_DIR}/network
  ${CMAKE_CURRENT_SOURCE_DIR}/config
  ${CMAKE_CURRENT_SOURCE_DIR}/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cjson)

# ============================================================= core
add_library(eclipse_core STATIC
  core/core.c
  vendor/cjson/cJSON.c)
target_include_directories(eclipse_core PUBLIC core platform vendor vendor/cjson)
target_link_libraries(eclipse_core PUBLIC ec_includes)
target_link_libraries(eclipse_core PUBLIC ec_flags ecdep::GLIB2 OpenSSL::Crypto)
find_package(Threads REQUIRED)
target_link_libraries(eclipse_core PUBLIC eclipse_platform Threads::Threads)

# Windows/MSYS2: pkg-config emits /mingw64 paths cmake cannot map - construct
# ecdep::* imported targets directly from the MSYS2 prefix (explicit layouts).
if(WIN32)
  set(MS2_PREFIX "" CACHE PATH "MSYS2 mingw64 prefix (Windows path)")
  find_program(CYGPATH_EXE cygpath)
  if(NOT MS2_PREFIX AND CYGPATH_EXE)
    execute_process(COMMAND ${CYGPATH_EXE} -m /mingw64 OUTPUT_VARIABLE MS2_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  endif()
  macro(ms2_target name incdirs libs)
    set(${name}_INCLUDES "")
    foreach(d ${incdirs})
      if(d MATCHES "^lib/")
        list(APPEND ${name}_INCLUDES "${MS2_PREFIX}/${d}")
      else()
        list(APPEND ${name}_INCLUDES "${MS2_PREFIX}/include/${d}")
      endif()
    endforeach()
    set(${name}_LIBS "")
    foreach(l ${libs})
      find_library(${name}_${l}_LIB ${l} PATHS ${MS2_PREFIX}/lib NO_DEFAULT_PATH)
      list(APPEND ${name}_LIBS ${${name}_${l}_LIB})
    endforeach()
    if(NOT TARGET ecdep::${name})
      add_library(ecdep::${name} INTERFACE IMPORTED)
    endif()
    set_target_properties(ecdep::${name} PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${${name}_INCLUDES}"
      INTERFACE_LINK_LIBRARIES "${${name}_LIBS}")
  endmacro()
  set(GLIB2_DIRS "glib-2.0;lib/glib-2.0/include")
  set(GIO2_DIRS "glib-2.0;lib/glib-2.0/include;gio-wn32lib-2.0" )
  ms2_target(GLIB2 "${GLIB2_DIRS}" "glib-2.0")
  ms2_target(GIO2 "${GLIB2_DIRS}" "gio-2.0;gobject-2.0;gmodule-2.0")
  ms2_target(LIBSSH "" "ssh")
  ms2_target(VTERM "" "vterm")
  if(ECLIPSE_BUILD_UI)
    set(GTK4_DIRS "gtk-4.0;gdk-pixbuf-2.0;pango-1.0;harfbuzz;cairo;graphene-1.0;lib/graphene-1.0/include;glib-2.0;lib/glib-2.0/include")
    ms2_target(GTK4 "${GTK4_DIRS}" "gtk-4")
  endif()
  link_directories(${MS2_PREFIX}/lib)
endif()

# Optional extra include dir (dev sandboxes with bundled libssh/libvterm)
set(ECLIPSE_EXTRA_INCLUDES "" CACHE PATH "Extra include dir for bundled deps")
if(ECLIPSE_EXTRA_INCLUDES)
  include_directories(${ECLIPSE_EXTRA_INCLUDES} ${ECLIPSE_EXTRA_INCLUDES}/libssh)
endif()

# ============================================================= platform
add_library(eclipse_platform STATIC platform/platform_posix.c platform/platform_win32.c)
if(WIN32)
  set_source_files_properties(platform/platform_posix.c PROPERTIES HEADER_FILE_ONLY ON)
else()
  set_source_files_properties(platform/platform_win32.c PROPERTIES HEADER_FILE_ONLY ON)
endif()
target_include_directories(eclipse_platform PUBLIC platform)
target_link_libraries(eclipse_platform PUBLIC ec_includes)

# ============================================================= security
add_library(eclipse_security STATIC security/security.c)
target_include_directories(eclipse_security PUBLIC security)
target_link_libraries(eclipse_security PUBLIC ec_includes)
target_link_libraries(eclipse_security PUBLIC eclipse_core ecdep::LIBSSH OpenSSL::Crypto)

# ============================================================= ssh engine
add_library(eclipse_ssh STATIC ssh/ssh_engine.c ssh/session.c)
target_include_directories(eclipse_ssh PUBLIC ssh)
target_link_libraries(eclipse_ssh PUBLIC ec_includes)
target_link_libraries(eclipse_ssh PUBLIC eclipse_core eclipse_security ecdep::LIBSSH eclipse_sftp eclipse_terminal eclipse_forward eclipse_config)

# ============================================================= sftp
add_library(eclipse_sftp STATIC sftp/sftp.c)
target_include_directories(eclipse_sftp PUBLIC sftp)
target_link_libraries(eclipse_sftp PUBLIC ec_includes)
target_link_libraries(eclipse_sftp PUBLIC eclipse_ssh ecdep::LIBSSH)

# ============================================================= terminal
add_library(eclipse_terminal STATIC terminal/term_core.c)
target_include_directories(eclipse_terminal PUBLIC terminal)
target_link_libraries(eclipse_terminal PUBLIC ec_includes)
target_link_libraries(eclipse_terminal PUBLIC eclipse_core ecdep::VTERM ZLIB::ZLIB)

# ============================================================= forwarding
add_library(eclipse_forward STATIC forwarding/fwd.c)
target_include_directories(eclipse_forward PUBLIC forwarding)
target_link_libraries(eclipse_forward PUBLIC ec_includes)
target_link_libraries(eclipse_forward PUBLIC eclipse_ssh ecdep::GIO2)

# ============================================================= network (proxy)
add_library(eclipse_network STATIC network/proxy.c)
target_include_directories(eclipse_network PUBLIC network)
target_link_libraries(eclipse_network PUBLIC ec_includes)
target_link_libraries(eclipse_network PUBLIC eclipse_core ecdep::GIO2)

# ============================================================= config repos
add_library(eclipse_config STATIC config/repos.c core/theme.c)
target_include_directories(eclipse_config PUBLIC config core)
target_link_libraries(eclipse_config PUBLIC ec_includes)
target_link_libraries(eclipse_config PUBLIC eclipse_core eclipse_security)

# ============================================================= GUI
if(ECLIPSE_BUILD_UI)
  add_library(eclipse_ui STATIC ui/ui.c ui/term_page.c)
  # GtkDialog-family APIs are deprecated since GTK 4.10 but still functional;
  # Adwaita migration tracked separately (docs/ARCHITECTURE.md TODO).
  target_compile_options(eclipse_ui PRIVATE -Wno-deprecated-declarations)
  target_include_directories(eclipse_ui PUBLIC ui)
target_link_libraries(eclipse_ui PUBLIC ec_includes)
  target_link_libraries(eclipse_ui PUBLIC
    eclipse_core eclipse_config eclipse_security eclipse_ssh eclipse_sftp
    eclipse_terminal eclipse_forward eclipse_session ecdep::GTK4)

  add_executable(eclipse-ssh app/main.c)
  target_link_libraries(eclipse-ssh PRIVATE eclipse_ui)
  install(TARGETS eclipse-ssh RUNTIME DESTINATION bin)
endif()

# ============================================================= CLI
add_executable(eclipse-ssh-cli cli/main.c)
target_link_libraries(eclipse-ssh-cli PRIVATE
  eclipse_core eclipse_config eclipse_security eclipse_ssh eclipse_sftp
  eclipse_forward eclipse_session eclipse_network ecdep::GIO2)
install(TARGETS eclipse-ssh-cli RUNTIME DESTINATION bin)

# ============================================================= tests
if(ECLIPSE_BUILD_TESTS)
  enable_testing()
  add_executable(test_unit tests/test_unit.c)
  target_link_libraries(test_unit PRIVATE
    eclipse_core eclipse_config eclipse_security eclipse_network eclipse_terminal eclipse_forward ZLIB::ZLIB ecdep::GIO2)
  add_test(NAME unit COMMAND test_unit)

  add_executable(test_roundtrip tests/test_roundtrip.c)
  target_link_libraries(test_roundtrip PRIVATE
    eclipse_core eclipse_config eclipse_security eclipse_ssh eclipse_sftp eclipse_forward)
  add_test(NAME ssh_roundtrip COMMAND test_roundtrip)
endif()

# ============================================================= packaging
set(CPACK_PACKAGE_NAME "eclipse-ssh")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "SecretArrow")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgtk-4-1, libssh-4, libvterm0, libssl3")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_GENERATOR "TGZ")
if(UNIX AND NOT APPLE)
  find_program(DPKG_FOUND dpkg)
  if(DPKG_FOUND)
    list(APPEND CPACK_GENERATOR "DEB")
  endif()
endif()
include(CPack)

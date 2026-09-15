# Eclipse SSH Desktop - source libraries
# Built up in stages: core -> ssh -> transfer -> terminal -> app (QML UI)

# ---------------------------------------------------------------------------
# common (header-only-ish helpers)
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# eclipse_core: settings, logging, database, profiles, credentials, shortcuts,
#               commands, snippets, theme
# ---------------------------------------------------------------------------
add_library(eclipse_core STATIC
    common/Utils.cpp
    core/Database.cpp
    core/logging/Logger.cpp
    core/settings/Settings.cpp
    core/profiles/ConnectionProfile.cpp
    core/profiles/ProfileStore.cpp
    core/profiles/PuttyImporter.cpp
    core/profiles/ProfileBundle.cpp
    core/credentials/CredentialBackends.cpp
    core/credentials/CredentialManager.cpp
    core/shortcuts/ShortcutManager.cpp
    core/commands/CommandRegistry.cpp
    core/snippets/SnippetStore.cpp
    core/ThemeManager.cpp
)

target_include_directories(eclipse_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(eclipse_core PUBLIC ${CMAKE_BINARY_DIR}/generated)

target_link_libraries(eclipse_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Sql
        OpenSSL::Crypto
)

if(NOT WIN32)
    # QtDBus does not exist on Windows builds; only the Linux Secret Service
    # credential backend uses it (sources are excluded above on WIN32).
    target_link_libraries(eclipse_core PUBLIC Qt6::DBus)
endif()

if(WIN32)
    target_sources(eclipse_core PRIVATE core/credentials/windows/WindowsCredentialStore.cpp)
    # Cred/Dpel API lives in advapi32
    target_link_libraries(eclipse_core PUBLIC advapi32)
else()
    target_sources(eclipse_core PRIVATE core/credentials/linux/SecretServiceStore.cpp)
endif()

eclipse_set_target_warnings(eclipse_core)

# ---------------------------------------------------------------------------
# eclipse_ssh: engine abstraction, libssh backend, host key mgmt, agent
# ---------------------------------------------------------------------------
add_library(eclipse_ssh STATIC
    ssh/HostKeyManager.cpp
    ssh/AgentClient.cpp
    ssh/ProxyDialer.cpp
    ssh/Bridge.cpp
    ssh/SshWorker.cpp
    ssh/SshSession.cpp
    ssh/ForwardManager.cpp
    ssh/JumpConnector.cpp
    ssh/Diagnostics.cpp
    ssh/backends/libssh/LibsshEngine.cpp
)

target_include_directories(eclipse_ssh PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eclipse_ssh
    PUBLIC
        eclipse_core
        Qt6::Network
        Qt6::Qml
        OpenSSL::Crypto
)
if(ECLIPSE_WITH_LIBSSH)
    target_link_libraries(eclipse_ssh PUBLIC PkgConfig::LIBSSH)
    target_compile_definitions(eclipse_ssh PUBLIC ECLIPSE_HAVE_LIBSSH)
    # Static libssh requires GSSAPI symbols; link the runtime library directly
    # when no dev symlink is available.
    find_library(GSSAPI_LIB NAMES gssapi_krb5)
    if(NOT GSSAPI_LIB)
        file(GLOB GSSAPI_LIB /usr/lib/x86_64-linux-gnu/libgssapi_krb5.so.*)
    endif()
    if(GSSAPI_LIB)
        target_link_libraries(eclipse_ssh PUBLIC ${GSSAPI_LIB})
    endif()
endif()

eclipse_set_target_warnings(eclipse_ssh)

# ---------------------------------------------------------------------------
# eclipse_transfer: transfer manager + jobs
# ---------------------------------------------------------------------------
add_library(eclipse_transfer STATIC
    transfer/TransferManager.cpp
    sftp/RemoteFsModel.cpp
)
target_link_libraries(eclipse_transfer
    PUBLIC
        eclipse_ssh
        Qt6::Concurrent
        Qt6::Quick
        ZLIB::ZLIB
)
eclipse_set_target_warnings(eclipse_transfer)

# ---------------------------------------------------------------------------
# eclipse_terminal: libvterm wrapper + QML terminal item
# ---------------------------------------------------------------------------
add_library(eclipse_terminal STATIC
    terminal/VtEmulator.cpp
    terminal/TermItem.cpp
    terminal/SixelDecoder.cpp
)
target_link_libraries(eclipse_terminal
    PUBLIC
        eclipse_ssh
        Qt6::Quick
        PkgConfig::VTERM
)
eclipse_set_target_warnings(eclipse_terminal)

# ---------------------------------------------------------------------------
# eclipse_archive: remote archive viewer core
# ---------------------------------------------------------------------------
add_library(eclipse_archive STATIC
    archive/Archive.cpp
)
target_link_libraries(eclipse_archive
    PUBLIC
        eclipse_ssh
        ZLIB::ZLIB
)
find_library(BZ2_LIB bz2)
find_library(LZMA_LIB lzma)
if(BZ2_LIB)
    target_compile_definitions(eclipse_archive PUBLIC ECLIPSE_HAVE_BZLIB)
    target_link_libraries(eclipse_archive PUBLIC ${BZ2_LIB})
endif()
if(LZMA_LIB)
    target_compile_definitions(eclipse_archive PUBLIC ECLIPSE_HAVE_LZMA)
    target_link_libraries(eclipse_archive PUBLIC ${LZMA_LIB})
endif()
eclipse_set_target_warnings(eclipse_archive)

# ---------------------------------------------------------------------------
# eclipse-ssh-desktop: the application (QML shell + C++ bridges)
# ---------------------------------------------------------------------------
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
file(GLOB ECLIPSE_QML_ABS ${CMAKE_SOURCE_DIR}/qml/*.qml ${CMAKE_SOURCE_DIR}/qml/pages/*.qml ${CMAKE_SOURCE_DIR}/qml/dialogs/*.qml ${CMAKE_SOURCE_DIR}/qml/components/*.qml)
foreach(qmlfile ${ECLIPSE_QML_ABS})
    file(RELATIVE_PATH relpath ${CMAKE_SOURCE_DIR} ${qmlfile})
    set_source_files_properties(${qmlfile} PROPERTIES QT_RESOURCE_ALIAS ${relpath})
endforeach()
set_source_files_properties(${CMAKE_SOURCE_DIR}/qml/IconMap.js PROPERTIES QT_RESOURCE_ALIAS "qml/IconMap.js")

set(ECLIPSE_QML_FILES
    ${CMAKE_SOURCE_DIR}/qml/Main.qml
    ${CMAKE_SOURCE_DIR}/qml/Theme.qml
    ${CMAKE_SOURCE_DIR}/qml/components/SnippetsPanel.qml
    ${CMAKE_SOURCE_DIR}/qml/pages/DashboardPage.qml
    ${CMAKE_SOURCE_DIR}/qml/pages/SessionPage.qml
    ${CMAKE_SOURCE_DIR}/qml/pages/FilesPage.qml
    ${CMAKE_SOURCE_DIR}/qml/pages/LogsPage.qml
    ${CMAKE_SOURCE_DIR}/qml/pages/SettingsPage.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/HostKeyDialog.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/QuickConnectDialog.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/ProfileDialog.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/PaletteDialog.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/ForwardDialog.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/RunnerDialog.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/DiagnosticsDialog.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/FirstRunWizard.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/AboutDialog.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/TransferPanel.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/ArchiveViewerDialog.qml
    ${CMAKE_SOURCE_DIR}/qml/dialogs/ImportDialog.qml
    ${CMAKE_SOURCE_DIR}/qml/components/TerminalFindBar.qml
    ${CMAKE_SOURCE_DIR}/qml/FlatButton.qml
    ${CMAKE_SOURCE_DIR}/qml/StatusDot.qml
    ${CMAKE_SOURCE_DIR}/qml/pages/MonitorPane.qml
    ${CMAKE_SOURCE_DIR}/qml/MaterialIcon.qml
    ${CMAKE_SOURCE_DIR}/qml/IconToolButton.qml
    ${CMAKE_SOURCE_DIR}/qml/IconMap.js
)

qt_add_executable(eclipse-ssh-desktop WIN32
    app/main.cpp
    app/AppController.h
    app/AppController.cpp
    app/TrayController.h
    app/TrayController.cpp
    net/TelnetSession.h
    net/TelnetSession.cpp
    net/SerialSession.h
    net/SerialSession.cpp
)

qt_add_qml_module(eclipse-ssh-desktop
    URI Eclipse
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES ${ECLIPSE_QML_FILES}
    NO_PLUGIN
    NO_IMPORT_SCAN
)

target_include_directories(eclipse-ssh-desktop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(eclipse-ssh-desktop PRIVATE
    eclipse_core
    eclipse_ssh
    eclipse_transfer
    eclipse_terminal
    eclipse_archive
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Network
)

# Optional serial console support (Qt6::SerialPort found in the root
# CMakeLists via OPTIONAL_COMPONENTS). When absent, SerialSession falls back
# to its compile-time stub with an identical API.
if(TARGET Qt6::SerialPort)
    target_link_libraries(eclipse-ssh-desktop PRIVATE Qt6::SerialPort)
    target_compile_definitions(eclipse-ssh-desktop PRIVATE ECLIPSE_HAVE_SERIALPORT)
endif()
set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/appicon.png PROPERTIES
    QT_RESOURCE_ALIAS "appicon.png")
qt_add_resources(eclipse-ssh-desktop "icons"
    PREFIX "/"
    FILES ${CMAKE_SOURCE_DIR}/resources/appicon.png)

# Material Design 3 runtime assets:
#  - EclipseMD3 Quick Controls style (qmldir + Templates components)
#  - Material Symbols Rounded icon fonts (subset, outlined + filled)
file(GLOB ECLIPSE_MD3_STYLE_FILES
    ${CMAKE_SOURCE_DIR}/qml/style/EclipseMD3/qmldir
    ${CMAKE_SOURCE_DIR}/qml/style/EclipseMD3/*.qml)
# Absolute source paths require explicit resource aliases (Qt6 requirement).
foreach(stylefile ${ECLIPSE_MD3_STYLE_FILES})
    get_filename_component(stylename ${stylefile} NAME)
    set_source_files_properties(${stylefile} PROPERTIES QT_RESOURCE_ALIAS ${stylename})
endforeach()
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/fonts/MaterialSymbolsRounded.ttf PROPERTIES
    QT_RESOURCE_ALIAS "MaterialSymbolsRounded.ttf")
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/fonts/MaterialSymbolsRounded-Fill.ttf PROPERTIES
    QT_RESOURCE_ALIAS "MaterialSymbolsRounded-Fill.ttf")
qt_add_resources(eclipse-ssh-desktop "md3style"
    PREFIX "/qt-project.org/imports/QtQuick/Controls/EclipseMD3"
    FILES ${ECLIPSE_MD3_STYLE_FILES})
qt_add_resources(eclipse-ssh-desktop "md3fonts"
    PREFIX "/fonts"
    FILES
        ${CMAKE_SOURCE_DIR}/resources/fonts/MaterialSymbolsRounded.ttf
        ${CMAKE_SOURCE_DIR}/resources/fonts/MaterialSymbolsRounded-Fill.ttf)

if(WIN32)
    # Windows binary identity: embed the multi-size application icon and a
    # VERSIONINFO block (description, version, publisher). Configured from the
    # CMake project version so it can never drift from the release version.
    set(ESD_ICON_RC_PATH "${CMAKE_SOURCE_DIR}/resources/eclipse-ssh-desktop.ico")
    configure_file("${CMAKE_SOURCE_DIR}/resources/app.rc.in"
                   "${CMAKE_BINARY_DIR}/generated/app.rc" @ONLY)
    target_sources(eclipse-ssh-desktop PRIVATE
        "${CMAKE_BINARY_DIR}/generated/app.rc")
endif()
eclipse_set_target_warnings(eclipse-ssh-desktop)

# ---------------------------------------------------------------------------
# ssh backends (each defines its own static library target)
# ---------------------------------------------------------------------------
if(ECLIPSE_WITH_LIBSSH2)
    add_subdirectory(ssh/backends/libssh2)
endif()

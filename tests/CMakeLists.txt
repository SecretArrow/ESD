# Eclipse SSH Desktop - automated tests
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Test)

# When building against a user-space sysroot (see scripts/setup-deps.sh),
# ensure test binaries can locate the Qt runtime libraries.
get_filename_component(_qt_lib_dir "${Qt6_DIR}/../.." ABSOLUTE)
list(APPEND CMAKE_BUILD_RPATH "${_qt_lib_dir}")

add_executable(test_hostkey_protocol HostKeyAndProtocolTest.cpp)
target_include_directories(test_hostkey_protocol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_hostkey_protocol PRIVATE
    eclipse_ssh
    eclipse_archive
    Qt6::Test
    OpenSSL::Crypto
)
add_test(NAME hostkey_protocol COMMAND $<TARGET_FILE:test_hostkey_protocol>)

add_executable(test_archive ArchiveTest.cpp)
target_include_directories(test_archive PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_archive PRIVATE
    eclipse_archive
    eclipse_ssh
    Qt6::Test
)
add_test(NAME archive COMMAND $<TARGET_FILE:test_archive>)

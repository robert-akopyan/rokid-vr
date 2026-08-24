cmake_minimum_required(VERSION 3.24)
project(RokidVR VERSION 0.1.4 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "RokidVR requires 64-bit Windows")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Pinned current OpenVR SDK revision (2026-03-27). The server-driver API is
# header-only, so avoid downloading/building the complete SDK.
set(OPENVR_INCLUDE_DIR ${CMAKE_BINARY_DIR}/_deps/openvr)
file(MAKE_DIRECTORY ${OPENVR_INCLUDE_DIR})
file(DOWNLOAD
  https://raw.githubusercontent.com/ValveSoftware/openvr/0924064316de3effbcd1acf1e309182a2deb1c05/headers/openvr_driver.h
  ${OPENVR_INCLUDE_DIR}/openvr_driver.h
  EXPECTED_HASH SHA256=1036efe998d63e82d1d3db2b32a2f58df4a8eeaf5280f50aaf28220ff60a40ab
  TLS_VERIFY ON
  STATUS OPENVR_DOWNLOAD_STATUS)
list(GET OPENVR_DOWNLOAD_STATUS 0 OPENVR_DOWNLOAD_CODE)
if(NOT OPENVR_DOWNLOAD_CODE EQUAL 0)
  list(GET OPENVR_DOWNLOAD_STATUS 1 OPENVR_DOWNLOAD_MESSAGE)
  message(FATAL_ERROR "Unable to download pinned openvr_driver.h: ${OPENVR_DOWNLOAD_MESSAGE}")
endif()

set(ROKIDVR_COMMON
  common/config.cpp
  common/log.cpp
  display/display.cpp
  rokid/fusion.cpp
  rokid/hid.cpp)

add_library(rokidvr_core STATIC ${ROKIDVR_COMMON})
target_include_directories(rokidvr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(rokidvr_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(rokidvr_core PRIVATE /W4 /permissive- /EHsc)
target_link_libraries(rokidvr_core PUBLIC setupapi hid d3d11 dxgi shell32)

add_library(rokidvr_presenter STATIC display/presenter.cpp)
target_compile_definitions(rokidvr_presenter PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(rokidvr_presenter PRIVATE /W4 /permissive- /EHsc)
target_link_libraries(rokidvr_presenter PUBLIC rokidvr_core d3dcompiler user32)

add_library(driver_rokidmax SHARED driver/driver.cpp)
target_include_directories(driver_rokidmax PRIVATE ${OPENVR_INCLUDE_DIR})
target_compile_definitions(driver_rokidmax PRIVATE ROKIDVR_DRIVER_BUILD)
target_compile_options(driver_rokidmax PRIVATE /W4 /permissive- /EHsc)
target_link_libraries(driver_rokidmax PRIVATE rokidvr_core rokidvr_presenter)
set_target_properties(driver_rokidmax PROPERTIES PREFIX "" OUTPUT_NAME "driver_rokidmax")

add_executable(RokidVRLauncher launcher/main.cpp)
target_compile_options(RokidVRLauncher PRIVATE /W4 /permissive- /EHsc)
target_link_libraries(RokidVRLauncher PRIVATE rokidvr_core shell32 comctl32)

enable_testing()
add_executable(rokidvr_tests tests/tests.cpp)
target_compile_options(rokidvr_tests PRIVATE /W4 /permissive- /EHsc)
target_link_libraries(rokidvr_tests PRIVATE rokidvr_core)
add_test(NAME rokidvr_tests COMMAND rokidvr_tests)
add_executable(driver_factory_smoke tests/driver_smoke.cpp)
target_compile_definitions(driver_factory_smoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(driver_factory_smoke PRIVATE /W4 /permissive- /EHsc)
add_dependencies(driver_factory_smoke driver_rokidmax)
add_test(NAME driver_factory_smoke COMMAND driver_factory_smoke $<TARGET_FILE:driver_rokidmax>)
add_executable(presenter_smoke tests/presenter_smoke.cpp)
target_compile_definitions(presenter_smoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(presenter_smoke PRIVATE /W4 /permissive- /EHsc)
target_link_libraries(presenter_smoke PRIVATE rokidvr_presenter)

set(PACKAGE_ROOT ${CMAKE_BINARY_DIR}/package)
set(DRIVER_PACKAGE ${PACKAGE_ROOT}/rokidmax)
add_custom_command(TARGET driver_rokidmax POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${DRIVER_PACKAGE}/bin/win64
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:driver_rokidmax> ${DRIVER_PACKAGE}/bin/win64/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/resources/driver.vrdrivermanifest ${DRIVER_PACKAGE}/
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources/settings ${DRIVER_PACKAGE}/resources/settings)
add_custom_command(TARGET RokidVRLauncher POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PACKAGE_ROOT}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:RokidVRLauncher> ${PACKAGE_ROOT}/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/scripts/install-driver.ps1 ${PACKAGE_ROOT}/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/scripts/uninstall-driver.ps1 ${PACKAGE_ROOT}/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/config.example.ini ${PACKAGE_ROOT}/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/README.md ${PACKAGE_ROOT}/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE ${PACKAGE_ROOT}/
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md ${PACKAGE_ROOT}/)

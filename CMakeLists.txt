# ==========================================
# KaleidoVR
# Automated background runner
# https://kalivr.com
# ==========================================

cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

if(NOT DEFINED _name)
  set(_name "kaleido-launcher")
endif()
if(NOT DEFINED _version)
  set(_version "1.0.0")
endif()

project(${_name} VERSION ${_version} LANGUAGES C CXX)

# The launcher is built on Win32 process and window APIs and has no portable equivalent.
if(NOT OS_WINDOWS)
  message(FATAL_ERROR "Kaleido Launcher is Windows-only; configuring on this platform is not supported.")
endif()

set(ENABLE_FRONTEND_API ON CACHE BOOL "Use obs-frontend-api" FORCE)
set(ENABLE_QT ON CACHE BOOL "Use Qt functionality" FORCE)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 COMPONENTS Widgets Core REQUIRED)

target_link_libraries(
  ${CMAKE_PROJECT_NAME} 
  PRIVATE 
  OBS::libobs 
  OBS::obs-frontend-api
  Qt6::Core
  Qt6::Widgets
)

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES 
  AUTOMOC ON 
  AUTOUIC ON 
  AUTORCC ON
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.cpp logo/kaleido.qrc)

set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES
  OUTPUT_NAME ${_name}
  VERSION 0
  SOVERSION ${PLUGIN_VERSION}
)

if(TARGET plugin-support)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE plugin-support)
endif()

# set_target_properties_plugin() is deliberately not used. Besides the version resource and
# rundir copy replicated below, it installs a second copy of the plugin in the
# %ProgramData%\obs-studio\plugins layout (<name>/bin/64bit and <name>/data). Kaleido
# Launcher ships into the OBS program folder, so that would duplicate the whole payload in
# every package.
string(TIMESTAMP CURRENT_YEAR "%Y")
configure_file(cmake/windows/resources/resource.rc.in "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.rc")
target_sources(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.rc")

# Mirror the built plugin into build_x64/rundir for quick local testing
add_custom_command(
  TARGET ${CMAKE_PROJECT_NAME}
  POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
    "$<$<CONFIG:Debug,RelWithDebInfo,Release>:$<TARGET_PDB_FILE:${CMAKE_PROJECT_NAME}>>"
    "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
  COMMENT "Copy ${CMAKE_PROJECT_NAME} to rundir"
  VERBATIM
)

# OBS program-folder layout. Every UI string is literal English and the module never calls
# obs_module_text(), so there are no resources to ship and no data/ tree is installed. If
# the UI is ever localized, resources belong in data/obs-plugins/${_name} because that is
# the only path obs_module_file() resolves.
install(TARGETS ${CMAKE_PROJECT_NAME}
  RUNTIME DESTINATION obs-plugins/64bit
  LIBRARY DESTINATION obs-plugins/64bit
)

install(FILES $<TARGET_PDB_FILE:${CMAKE_PROJECT_NAME}>
  DESTINATION obs-plugins/64bit
  OPTIONAL
)

# CPack Packaging Configuration
# Sourced from buildspec.json so the package version cannot drift from the plugin version
set(CPACK_PACKAGE_NAME ${_name})
set(CPACK_PACKAGE_VERSION ${_version})
set(CPACK_SYSTEM_NAME "win64")

set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)

# NSIS Specific Settings
set(CPACK_NSIS_DISPLAY_NAME "Kaleido Launcher for OBS")
set(CPACK_NSIS_PACKAGE_NAME "Kaleido Launcher")
set(CPACK_NSIS_REQUEST_EXECUTION_LEVEL "admin")
set(CPACK_NSIS_INSTALLED_ICON_NAME "obs-plugins\\\\64bit\\\\kaleido-launcher.dll")

# Metadata
set(CPACK_PACKAGE_VENDOR "KaleidoVR")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://kalivr.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Automated background runner and plugin for OBS Studio")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/CPackOptions.cmake")

# CMAKE_INSTALL_PREFIX defaults to "$ENV{ALLUSERSPROFILE}/obs-studio/plugins", which keeps
# the native backslash from ALLUSERSPROFILE. CPack copies that verbatim into
# CPackConfig.cmake, where "\P" is an invalid escape that breaks every generator. Package
# relative to the archive root so the payload lands at obs-plugins/64bit instead.
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
set(CPACK_INSTALL_PREFIX "/")

# Keep generated packages next to the build rather than at the drive root
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")

include(CPack)
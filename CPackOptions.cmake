# Evaluated by CPack per generator. CMAKE_BINARY_DIR is not defined in this context, so
# the output location is set from CMakeLists.txt instead of here.
# obs-plugins/ sits at the archive root so the ZIP can be extracted straight over an OBS
# install. A top-level <name>-<version>-win64 folder would have to be stepped into first.
if(CPACK_GENERATOR MATCHES "ZIP")
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
endif()

# - Find QatZstd package
# Find the QatZstd compression library and includes
#
# QatZstd_INCLUDE_DIR - where to find qatseqprod.h
# QatZstd_LIBRARIES - List of libraries when using QatZstd.
# QatZstd_FOUND - True if QatZstd found.

find_path(QatZstd_INCLUDE_DIR NAMES qatseqprod.h HINTS /usr/local/include)
find_library(QatZstd_LIBRARIES NAMES qatseqprod HINTS /usr/local/lib/)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(QatZstd DEFAULT_MSG QatZstd_LIBRARIES QatZstd_INCLUDE_DIR)

mark_as_advanced(
  QatZstd_LIBRARIES
  QatZstd_INCLUDE_DIR)

if(QatZstd_FOUND AND NOT TARGET QatZstd::QatZstd)
  add_library(QatZstd::QatZstd SHARED IMPORTED)
  set_target_properties(QatZstd::QatZstd PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${QatZstd_INCLUDE_DIR}"
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${QatZstd_LIBRARIES}")
endif()
# zstd

option(WITH_SYSTEM_ZSTD "use prebuilt libzstd in system" OFF)

if(WITH_SYSTEM_ZSTD)
  find_package(Zstd 1.5.5 REQUIRED)
else()
  include(BuildZstd)
  build_Zstd()
endif()

file(GLOB_RECURSE SRC_FILES *.cc)
file(GLOB_RECURSE H_FILES *.h)

#set(zstd_sources
#  CompressionPluginZstd.cc)

set(zstd_sources
  ${SRC_FILES} ${H_FILES})

add_library(ceph_zstd SHARED ${zstd_sources})
target_link_libraries(ceph_zstd PRIVATE Zstd::Zstd qatseqprod $<$<PLATFORM_ID:Windows>:ceph-common>)
set_target_properties(ceph_zstd PROPERTIES
  VERSION 2.0.0
  SOVERSION 2
  INSTALL_RPATH "")
install(TARGETS ceph_zstd DESTINATION ${compressor_plugin_dir})

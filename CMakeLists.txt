cmake_minimum_required (VERSION 2.6)
project (PeerspeakDiscovery)

find_package (PkgConfig REQUIRED)
pkg_check_modules (GLIBMM REQUIRED glibmm-2.4)

include_directories (${GLIBMM_INCLUDE_DIRS})
link_directories (${GLIBMM_LIBRARY_DIRS})
add_definitions (${GLIBMM_CFLAGS_OTHER})

include_directories (include)

set (SOURCES
    src/main.cc
    src/connection_handler.cc
    src/connection.cc)

add_executable (ps-discovery ${SOURCES})

target_link_libraries (ps-discovery ${GLIBMM_LIBRARIES})
set_property (TARGET ps-discovery PROPERTY CXX_STANDARD 14)

install (TARGETS ps-discovery DESTINATION bin)

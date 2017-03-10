cmake_minimum_required (VERSION 2.6)
project (PeerspeakDiscovery)

include_directories (include)

set (SOURCES
    src/main.cc
    src/connection_handler.cc
    src/connection.cc)

add_executable (ps-discovery ${SOURCES})

set_property (TARGET ps-discovery PROPERTY CXX_STANDARD 14)

install (TARGETS ps-discovery DESTINATION bin)

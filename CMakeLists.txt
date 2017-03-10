cmake_minimum_required (VERSION 2.6)
project (PeerspeakDiscovery)

find_package (Threads)

include_directories (include)

add_definitions (-DASIO_STANDALONE)

set (SOURCES
    src/main.cc
    src/connection_handler.cc
    src/connection.cc)

add_executable (ps-discovery ${SOURCES})

target_link_libraries (ps-discovery ${CMAKE_THREAD_LIBS_INIT})
set_property (TARGET ps-discovery PROPERTY CXX_STANDARD 14)

install (TARGETS ps-discovery DESTINATION bin)

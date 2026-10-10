cmake_minimum_required(VERSION 3.16)

project(SPSCQueue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SPSCQueue main.cpp
    spscqueue.h
)

include(GNUInstallDirs)
install(TARGETS SPSCQueue
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

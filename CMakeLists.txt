cmake_minimum_required(VERSION 2.4)

PROJECT(miniws)

find_library(BOOST >= 1.46.0)
add_library(miniws server.cpp sha1.cpp base64.cpp)
find_package(Boost COMPONENTS system thread REQUIRED)
target_link_libraries(miniws ${Boost_SYSTEM_LIBRARY})

add_subdirectory(samples)
cmake_minimum_required(VERSION 3.10)
project(tcpredir)
set (CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

add_executable(tcpredir tcpredir.cpp)
target_link_libraries(tcpredir Threads::Threads)
target_link_libraries(tcpredir Boost::system)

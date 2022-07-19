cmake_minimum_required(VERSION 3.10)
project(tcpredir)
set (CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-Werror -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

find_package(Threads REQUIRED)
find_package(Boost 1.65 REQUIRED COMPONENTS system)
if (Boost_VERSION MATCHES "^[0-9]+$") # 106501
    message("Fixing Boost_VERSION")
    math(EXPR Boost_VERSION_MAJOR "${Boost_VERSION} / 100000")
    math(EXPR Boost_VERSION "${Boost_VERSION} % 10000")
    math(EXPR Boost_VERSION_MINOR "${Boost_VERSION} / 100")
    math(EXPR Boost_VERSION_PATCH "${Boost_VERSION} % 100")
    set(Boost_VERSION "${Boost_VERSION_MAJOR}.${Boost_VERSION_MINOR}.${Boost_VERSION_PATCH}")
    set(Boost_VERSION_STRING ${Boost_VERSION})
endif()

add_executable(tcpredir tcpredir.cpp)
target_include_directories(tcpredir PRIVATE ${Boost_INCLUDE_DIR})
target_include_directories(tcpredir PRIVATE ${Boost_INCLUDE_DIR})
target_link_libraries(tcpredir PRIVATE Threads::Threads)
target_link_libraries(tcpredir PRIVATE Boost::system) # necessary for ubuntu 18.04

install(TARGETS tcpredir DESTINATION bin)

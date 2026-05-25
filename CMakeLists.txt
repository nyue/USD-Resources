cmake_minimum_required(VERSION 3.9 FATAL_ERROR)
project(USD-resources VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17) # c++ 17 used for some convenience functions
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Some RPATH stuff. Don't think this is how you do it....
#set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
#set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
#set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# Set some compile definitions. 
# add_definitions(-D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION) # Enables linking USD libraries with C++17

include(cmake/createTarget.cmake)

option(BUILD_USD_PLUGIN "Build the USD plugin" OFF)
option(BUILD_HOUDINI_PLUGIN "Build the Houdini plugin" OFF)
# set(BUILD_ALL 1)

if ("${BUILD_USD_PLUGIN}" OR "${BUILD_HOUDINI_PLUGIN}")
    add_subdirectory(hairProc)
endif()


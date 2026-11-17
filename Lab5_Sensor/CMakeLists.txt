cmake_minimum_required(VERSION 3.16.0)

file(GLOB RAINMAKER_COMPONENT_DIRS
     "${CMAKE_CURRENT_LIST_DIR}/../Lab5_Plug/managed_components/*")
list(APPEND EXTRA_COMPONENT_DIRS ${RAINMAKER_COMPONENT_DIRS})
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Lab5_Sensor)

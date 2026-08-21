cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS ${CMAKE_SOURCE_DIR}/main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kakata_ctrlr_433)

set(PARTITION_TABLE_CSV ${CMAKE_SOURCE_DIR}/partitions.csv)
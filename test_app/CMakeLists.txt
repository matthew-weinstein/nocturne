cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(TEST_COMPONENTS audio crypto storage hmi system)
set(COMPONENTS main ${TEST_COMPONENTS})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nocturne_test_app)

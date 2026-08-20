# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(ENV{PYTHONUTF8} "1")
set(ENV{PYTHONIOENCODING} "utf-8")
set(CCACHE_ENABLE 0)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(0-0sample_project)

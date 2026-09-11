cmake_minimum_required(VERSION 3.16)

# Version string embedded in the app descriptor (shown on the OTA web page and /status)
set(PROJECT_VER "1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(weather_display)

cmake_minimum_required(VERSION 3.25)
project(PredictiveInputBuffer VERSION 1.0.0)

# Vô hiệu hóa fmt strict enforcement để tránh lỗi NDK Clang trên Android
add_compile_definitions(FMT_ENFORCE_COMPILE_STRING=0)
add_compile_definitions(GEODE_DISABLE_FMT_LOGGING)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK environment variable is not set.")
endif()

include($ENV{GEODE_SDK}/cmake/Geode.cmake)

add_geode_mod(PredictiveInputBuffer)

target_sources(PredictiveInputBuffer PRIVATE
    src/main.cpp
)

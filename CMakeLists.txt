cmake_minimum_required(VERSION 3.25)
project(PredictiveInputBuffer)

# Tắt sạch việc compile time format string của fmt trên Android
add_compile_definitions(FMT_ENFORCE_COMPILE_STRING=0)
add_compile_definitions(GEODE_DISABLE_FMT_LOGGING)

include($ENV{GEODE_SDK}/cmake/Geode.cmake)

add_geode_mod(PredictiveInputBuffer)

target_sources(PredictiveInputBuffer PRIVATE
    src/main.cpp
)

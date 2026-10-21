cmake_minimum_required(VERSION 3.25)
project(PredictiveInputBuffer VERSION 1.0.0)

find_package(Geode REQUIRED)

add_geode_mod(PredictiveInputBuffer)

target_sources(PredictiveInputBuffer PRIVATE
    src/main.cpp
)

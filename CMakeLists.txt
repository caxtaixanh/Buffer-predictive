cmake_minimum_required(VERSION 3.25)
project(PredictiveInputBuffer CXX)

find_package(Geode REQUIRED)

add_geode_mod(predictive-input-buffer)

target_sources(predictive-input-buffer PRIVATE
    src/main.cpp
)

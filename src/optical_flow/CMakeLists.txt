cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(LTROpticalFlowProbe LANGUAGES CXX)
    enable_testing()
endif()

add_executable(ltr_optical_flow_probe probe.cpp)
target_compile_features(ltr_optical_flow_probe PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(ltr_optical_flow_probe PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
else()
    target_compile_options(ltr_optical_flow_probe PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME ltr_optical_flow_probe COMMAND ltr_optical_flow_probe)

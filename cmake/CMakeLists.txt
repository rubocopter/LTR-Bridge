add_executable(ltr_temporal_harness WIN32
    ../src/harness/main.cpp
    ../src/harness/temporal_frame.h
)
target_compile_features(ltr_temporal_harness PRIVATE cxx_std_20)
target_compile_definitions(ltr_temporal_harness PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(ltr_temporal_harness PRIVATE d3d11 dxgi d3dcompiler)
if(MSVC)
    target_compile_options(ltr_temporal_harness PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
endif()

add_test(NAME ltr_temporal_harness_selftest
    COMMAND ${CMAKE_COMMAND}
        -DHARNESS=$<TARGET_FILE:ltr_temporal_harness>
        -DREPORT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_LIST_DIR}/run_selftest.cmake
)

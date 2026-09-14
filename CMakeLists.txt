cmake_minimum_required(VERSION 3.24)
project(LTRBridge LANGUAGES CXX)
enable_testing()
add_subdirectory(cmake)
add_subdirectory(src/optical_flow)

option(LTR_ENABLE_XESS_NATIVE_AA_PROBE "Build the optional XeSS Native AA D3D12 probe" OFF)
set(LTR_XESS_SDK_ROOT "" CACHE PATH "Path to an external Intel XeSS SDK checkout")
set(LTR_XESS_PROBE_WIDTH "256" CACHE STRING "XeSS Native AA probe width")
set(LTR_XESS_PROBE_HEIGHT "144" CACHE STRING "XeSS Native AA probe height")
set(LTR_XESS_PROBE_FRAMES "12" CACHE STRING "XeSS Native AA probe frame count")

if(LTR_ENABLE_XESS_NATIVE_AA_PROBE)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "ltr_xess_native_aa_probe requires 64-bit Windows")
    endif()
    if(NOT LTR_XESS_SDK_ROOT)
        message(FATAL_ERROR "LTR_XESS_SDK_ROOT must point to an external XeSS SDK checkout")
    endif()
    foreach(_numeric IN ITEMS LTR_XESS_PROBE_WIDTH LTR_XESS_PROBE_HEIGHT LTR_XESS_PROBE_FRAMES)
        if(NOT ${_numeric} MATCHES "^[1-9][0-9]*$")
            message(FATAL_ERROR "${_numeric} must be a positive integer")
        endif()
    endforeach()

    set(_ltr_xess_header "${LTR_XESS_SDK_ROOT}/inc/xess/xess_d3d12.h")
    set(_ltr_xess_lib "${LTR_XESS_SDK_ROOT}/lib/libxess.lib")
    set(_ltr_xess_dll "${LTR_XESS_SDK_ROOT}/bin/libxess.dll")
    foreach(_required IN ITEMS "${_ltr_xess_header}" "${_ltr_xess_lib}" "${_ltr_xess_dll}")
        if(NOT EXISTS "${_required}")
            message(FATAL_ERROR "Required XeSS SDK artifact not found: ${_required}")
        endif()
    endforeach()

    add_executable(ltr_xess_native_aa_probe src/xess_native_aa_probe/probe.cpp)
    target_compile_features(ltr_xess_native_aa_probe PRIVATE cxx_std_20)
    target_compile_definitions(ltr_xess_native_aa_probe PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        LTR_XESS_PROBE_WIDTH=${LTR_XESS_PROBE_WIDTH}
        LTR_XESS_PROBE_HEIGHT=${LTR_XESS_PROBE_HEIGHT}
        LTR_XESS_PROBE_FRAMES=${LTR_XESS_PROBE_FRAMES})
    target_include_directories(ltr_xess_native_aa_probe PRIVATE "${LTR_XESS_SDK_ROOT}/inc")
    target_link_libraries(ltr_xess_native_aa_probe PRIVATE d3d12 dxgi dxguid "${_ltr_xess_lib}")
    if(MSVC)
        target_compile_options(ltr_xess_native_aa_probe PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    endif()
    add_custom_command(TARGET ltr_xess_native_aa_probe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_ltr_xess_dll}"
            "$<TARGET_FILE_DIR:ltr_xess_native_aa_probe>/libxess.dll")
    add_test(NAME ltr_xess_native_aa_probe COMMAND ltr_xess_native_aa_probe)
endif()

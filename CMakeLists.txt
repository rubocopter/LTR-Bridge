cmake_minimum_required(VERSION 3.24)
project(LTRBridge LANGUAGES CXX)
enable_testing()
add_subdirectory(cmake)
add_subdirectory(src/optical_flow)

option(LTR_ENABLE_X86_X64_BRIDGE_PROBE "Build one side of the D3D11 x86 -> D3D12 x64 transport probe" OFF)
if(LTR_ENABLE_X86_X64_BRIDGE_PROBE)
    if(NOT WIN32)
        message(FATAL_ERROR "x86/x64 bridge probe requires Windows")
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_executable(ltr_x86_x64_bridge_producer
            src/x86_x64_bridge_probe/producer.cpp
            src/x86_x64_bridge_probe/common.h)
        target_compile_features(ltr_x86_x64_bridge_producer PRIVATE cxx_std_20)
        target_compile_definitions(ltr_x86_x64_bridge_producer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
        target_link_libraries(ltr_x86_x64_bridge_producer PRIVATE d3d11 dxgi d3dcompiler)
        if(MSVC)
            target_compile_options(ltr_x86_x64_bridge_producer PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
        endif()
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(ltr_x86_x64_bridge_consumer
            src/x86_x64_bridge_probe/consumer.cpp
            src/x86_x64_bridge_probe/common.h)
        target_compile_features(ltr_x86_x64_bridge_consumer PRIVATE cxx_std_20)
        target_compile_definitions(ltr_x86_x64_bridge_consumer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
        target_link_libraries(ltr_x86_x64_bridge_consumer PRIVATE d3d12 dxgi dxguid d3dcompiler)
        if(MSVC)
            target_compile_options(ltr_x86_x64_bridge_consumer PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
        endif()
    else()
        message(FATAL_ERROR "unsupported pointer size for x86/x64 bridge probe")
    endif()
endif()

option(LTR_ENABLE_D3D9_D3D11_SHARE_PROBE "Build the classic D3D9/D3D9Ex -> D3D11 shared-texture probe" OFF)
if(LTR_ENABLE_D3D9_D3D11_SHARE_PROBE)
    if(NOT WIN32)
        message(FATAL_ERROR "D3D9/D3D11 sharing probe requires Windows")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "D3D9/D3D11 sharing probe currently targets Win32/x86")
    endif()
    add_executable(ltr_d3d9_d3d11_share_probe src/d3d9_d3d11_share_probe/probe.cpp)
    target_compile_features(ltr_d3d9_d3d11_share_probe PRIVATE cxx_std_20)
    target_compile_definitions(ltr_d3d9_d3d11_share_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(ltr_d3d9_d3d11_share_probe PRIVATE d3d9 d3d11 dxgi)
    if(MSVC)
        target_compile_options(ltr_d3d9_d3d11_share_probe PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    endif()
endif()

option(LTR_ENABLE_OPENXR_RUNTIME_PROBE "Build the optional x64 OpenXR runtime/bootstrap probe" OFF)
set(LTR_OPENXR_SDK_ROOT "" CACHE PATH "Path to an external Khronos OpenXR SDK checkout")

if(LTR_ENABLE_OPENXR_RUNTIME_PROBE)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "ltr_openxr_runtime_probe requires 64-bit Windows")
    endif()
    if(NOT LTR_OPENXR_SDK_ROOT)
        message(FATAL_ERROR "LTR_OPENXR_SDK_ROOT must point to an external Khronos OpenXR SDK checkout")
    endif()
    set(_ltr_openxr_header "${LTR_OPENXR_SDK_ROOT}/include/openxr/openxr.h")
    set(_ltr_openxr_platform_header "${LTR_OPENXR_SDK_ROOT}/include/openxr/openxr_platform.h")
    foreach(_required IN ITEMS "${_ltr_openxr_header}" "${_ltr_openxr_platform_header}")
        if(NOT EXISTS "${_required}")
            message(FATAL_ERROR "Required OpenXR SDK header not found: ${_required}")
        endif()
    endforeach()

    add_executable(ltr_openxr_runtime_probe src/openxr_runtime_probe/probe.cpp)
    target_compile_features(ltr_openxr_runtime_probe PRIVATE cxx_std_20)
    target_compile_definitions(ltr_openxr_runtime_probe PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        XR_USE_PLATFORM_WIN32
        XR_USE_GRAPHICS_API_D3D11
        XR_USE_GRAPHICS_API_D3D12
        XR_NO_PROTOTYPES)
    target_include_directories(ltr_openxr_runtime_probe PRIVATE "${LTR_OPENXR_SDK_ROOT}/include")
    target_link_libraries(ltr_openxr_runtime_probe PRIVATE dxgi)
    if(MSVC)
        target_compile_options(ltr_openxr_runtime_probe PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus)
    endif()
endif()

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

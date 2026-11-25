cmake_minimum_required(VERSION 3.24)
project(catherine_dx12 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "catherine_dx12 requires Windows and the Windows SDK")
endif()

set(NVN_CPU_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../src/nvn")
add_library(catherine_dx12 STATIC renderer.cpp binding_layout.cpp shader_binary.cpp morph_replay.cpp native_replay.cpp
                                 nvn_image.cpp nvn_draw_state.cpp nvn_guest_upload.cpp nvn_shader_bindings.cpp
                                 "${NVN_CPU_SOURCE}/nvn_tiling.cpp" "${NVN_CPU_SOURCE}/nvn_astc.cpp"
                                 "${NVN_CPU_SOURCE}/nvn_spirv_reflect.cpp")
target_include_directories(catherine_dx12 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${NVN_CPU_SOURCE}")
target_compile_features(catherine_dx12 PUBLIC cxx_std_20)
target_compile_definitions(catherine_dx12 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(catherine_dx12 PRIVATE d3d12 d3dcompiler dxgi user32)

if(MSVC)
    target_compile_options(catherine_dx12 PRIVATE /W4)
endif()

include(CTest)
set(CATHERINE_DX12_REPLAY_SHADERS "" CACHE PATH "Directory of locally translated Catherine replay DXIL")
set(CATHERINE_DX12_REPLAY_SPIRV "" CACHE PATH "Directory of original Catherine replay SPIR-V")
if(BUILD_TESTING)
    add_executable(catherine_dx12_smoke tests/smoke.cpp)
    target_compile_definitions(catherine_dx12_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(catherine_dx12_smoke PRIVATE catherine_dx12)
    add_test(NAME dx12_warp_pixels COMMAND catherine_dx12_smoke --warp)
    add_test(NAME dx12_hardware_pixels COMMAND catherine_dx12_smoke)
    add_test(NAME dx12_window_resize COMMAND catherine_dx12_smoke --window)
    add_test(NAME dx12_window_immediate COMMAND catherine_dx12_smoke --window-immediate)
    set_tests_properties(dx12_warp_pixels dx12_hardware_pixels dx12_window_resize dx12_window_immediate
        PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    if(CATHERINE_DX12_REPLAY_SHADERS)
        add_executable(catherine_dx12_replay_smoke tests/replay_smoke.cpp)
        target_compile_definitions(catherine_dx12_replay_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(catherine_dx12_replay_smoke PRIVATE catherine_dx12)
        if(CATHERINE_DX12_REPLAY_SPIRV)
            set(replay_spirv_args --spirv "${CATHERINE_DX12_REPLAY_SPIRV}")
        endif()
        add_test(NAME dx12_nvn_hardware
            COMMAND catherine_dx12_replay_smoke "${CATHERINE_DX12_REPLAY_SHADERS}" ${replay_spirv_args})
        add_test(NAME dx12_nvn_warp
            COMMAND catherine_dx12_replay_smoke "${CATHERINE_DX12_REPLAY_SHADERS}" --warp ${replay_spirv_args})
        set_tests_properties(dx12_nvn_hardware dx12_nvn_warp PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    endif()
endif()

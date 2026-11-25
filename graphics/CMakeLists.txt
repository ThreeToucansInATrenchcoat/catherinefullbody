cmake_minimum_required(VERSION 3.24)
project(catherine_graphics LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
file(GLOB NVN_SOURCES CONFIGURE_DEPENDS src/nvn/*.cpp src/vk_*.cpp)
add_library(catherine_graphics STATIC ${NVN_SOURCES})
target_compile_definitions(catherine_graphics PRIVATE VK_USE_PLATFORM_WIN32_KHR VK_NO_PROTOTYPES NOMINMAX)
target_include_directories(catherine_graphics PRIVATE include src "$ENV{VULKAN_SDK}/Include")
add_executable(linkcheck src/linkcheck.cpp src/adapter.cpp src/platform_win.cpp src/game.cpp src/bridge.cpp)
target_include_directories(linkcheck PRIVATE include src "$ENV{VULKAN_SDK}/Include")
target_link_libraries(linkcheck PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,catherine_graphics>")

target_link_options(linkcheck PRIVATE -fuse-ld=lld -Xlinker /errorlimit:0)
target_link_libraries(linkcheck PRIVATE xinput xaudio2 user32 gdi32 shell32)

target_compile_definitions(linkcheck PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)


cmake_minimum_required(VERSION 3.20)
project(casino-engine-client VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(casino-engine WIN32
    src/main.cpp
    src/app.cpp
    src/net/api_client.cpp
    src/net/fairness.cpp
    src/ui/theme.cpp
    src/ui/widgets.cpp
)

target_include_directories(casino-engine PRIVATE src)

target_compile_definitions(casino-engine PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(casino-engine PRIVATE /W4 /permissive- /utf-8)
endif()

target_link_libraries(casino-engine PRIVATE
    imgui::imgui
    nlohmann_json::nlohmann_json
    d3d11
    dxgi
    winhttp
    bcrypt
    dwmapi
)

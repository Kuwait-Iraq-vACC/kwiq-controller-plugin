cmake_minimum_required(VERSION 3.21)
project(KWIQPlugin LANGUAGES CXX)

# EuroScope plugins are 32-bit: configure with -A Win32 (see .github/workflows/build.yml).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")   # /MD, same as VCH

find_package(CURL CONFIG REQUIRED)               # from vcpkg (static, Windows schannel TLS)
find_package(nlohmann_json CONFIG REQUIRED)      # from vcpkg

# KWIQPluginCore.dll: the plugin itself
add_library(KWIQPluginCore SHARED
    KWIQPlugin.cpp
    VatsimAPI.cpp
    keylink.cpp          # receives the API key from myKWIQ
    requestMenu.cpp      # VCH
    internet.cpp         # VCH
    usefulStuff.cpp      # VCH
)

target_include_directories(KWIQPluginCore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/libs)
# Set by GitHub Actions to its run number, so each build has its own version (0.5.0.42)
set(KWIQ_BUILD_NUMBER 0 CACHE STRING "Build number added to the plugin version")
target_compile_definitions(KWIQPluginCore PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS KWIQ_BUILD_NUMBER=${KWIQ_BUILD_NUMBER})
target_link_libraries(KWIQPluginCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/EuroScopePlugInDll.lib
    CURL::libcurl
    nlohmann_json::nlohmann_json
    wininet                                       # VCH
    winmm                                         # PlaySound (sector file alert)
    ws2_32                                        # API key from myKWIQ (keylink.cpp)
    version                                       # GetFileVersionInfo (EuroScope version check)
    comctl32                                      # the SRD search results list
)
set_target_properties(KWIQPluginCore PROPERTIES PREFIX "" OUTPUT_NAME "KWIQPluginCore")

# KWIQPlugin.dll: the one file controllers download and load. KWIQPluginCore.dll is built into it,
# kept up to date in %APPDATA%\KWIQ\plugin and loaded from there (loader.cpp)
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/core_$<CONFIG>.rc
     CONTENT "KWIQCORE RCDATA \"$<TARGET_FILE:KWIQPluginCore>\"\n")
add_library(KWIQPlugin SHARED loader.cpp ${CMAKE_CURRENT_BINARY_DIR}/core_$<CONFIG>.rc)
add_dependencies(KWIQPlugin KWIQPluginCore)
target_include_directories(KWIQPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/libs)
target_compile_definitions(KWIQPlugin PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS KWIQ_BUILD_NUMBER=${KWIQ_BUILD_NUMBER})
target_link_libraries(KWIQPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/EuroScopePlugInDll.lib winhttp)
set_target_properties(KWIQPlugin PROPERTIES PREFIX "" OUTPUT_NAME "KWIQPlugin")

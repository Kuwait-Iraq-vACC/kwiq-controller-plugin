cmake_minimum_required(VERSION 3.21)
project(KWIQPlugin LANGUAGES CXX)

# EuroScope plugins are 32-bit: configure with -A Win32 (see .github/workflows/build.yml).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")   # /MD, same as VCH

find_package(CURL CONFIG REQUIRED)               # from vcpkg (static, Windows schannel TLS)
find_package(nlohmann_json CONFIG REQUIRED)      # from vcpkg

# Warnings (#41): KWIQ's own sources are built at /W4, and CI fails on any warning in them. Vendored code
# (VCH, the Slots-Plugin's VatsimAPI.cpp) keeps the default level. Headers from libs/ and vcpkg are external
# (SYSTEM, so /external:I and /external:W0), so their warnings never count.
set(KWIQ_OWN_SOURCES KWIQPlugin.cpp keylink.cpp loader.cpp)
set_source_files_properties(${KWIQ_OWN_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4")
# WIN32_LEAN_AND_MEAN is empty, as VCH's stdafx.h defines it, so it is never redefined (C4005)
set(KWIQ_DEFINITIONS WIN32_LEAN_AND_MEAN= _CRT_SECURE_NO_WARNINGS)

# KWIQPluginCore.dll: the plugin itself
add_library(KWIQPluginCore SHARED
    KWIQPlugin.cpp
    VatsimAPI.cpp
    keylink.cpp          # receives the API key from myKWIQ
    requestMenu.cpp      # VCH
    internet.cpp         # VCH
    usefulStuff.cpp      # VCH
)

target_include_directories(KWIQPluginCore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(KWIQPluginCore SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs)
# Set by GitHub Actions to its run number, so each build has its own version (0.5.0.42)
set(KWIQ_BUILD_NUMBER 0 CACHE STRING "Build number added to the plugin version")
target_compile_definitions(KWIQPluginCore PRIVATE ${KWIQ_DEFINITIONS} KWIQ_BUILD_NUMBER=${KWIQ_BUILD_NUMBER})
target_link_libraries(KWIQPluginCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/EuroScopePlugInDll.lib
    CURL::libcurl
    nlohmann_json::nlohmann_json
    wininet                                       # VCH
    winmm                                         # PlaySound (sector file alert)
    ws2_32                                        # API key from myKWIQ (keylink.cpp)
    version                                       # GetFileVersionInfo (EuroScope version check)
)
set_target_properties(KWIQPluginCore PROPERTIES PREFIX "" OUTPUT_NAME "KWIQPluginCore")

# KWIQPlugin.dll: the one file controllers download and load. KWIQPluginCore.dll is built into it,
# kept up to date in %APPDATA%\KWIQ\plugin and loaded from there (loader.cpp)
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/core_$<CONFIG>.rc
     CONTENT "KWIQCORE RCDATA \"$<TARGET_FILE:KWIQPluginCore>\"\n")
add_library(KWIQPlugin SHARED loader.cpp ${CMAKE_CURRENT_BINARY_DIR}/core_$<CONFIG>.rc)
add_dependencies(KWIQPlugin KWIQPluginCore)
target_include_directories(KWIQPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(KWIQPlugin SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs)
target_compile_definitions(KWIQPlugin PRIVATE ${KWIQ_DEFINITIONS} NOMINMAX KWIQ_BUILD_NUMBER=${KWIQ_BUILD_NUMBER})
target_link_libraries(KWIQPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/EuroScopePlugInDll.lib winhttp)
set_target_properties(KWIQPlugin PROPERTIES PREFIX "" OUTPUT_NAME "KWIQPlugin")

# Each KWIQ header must compile on its own (#41), e.g. windows.h before EuroScopePlugIn.h: one generated .cpp
# per header, built at /W4 with the plugin's definitions. Nothing is linked from it.
set(KWIQ_HEADERS kwiq_logic.hpp keylink.hpp VatsimAPI.h version.h)
set(KWIQ_HEADER_CHECKS "")
foreach(h IN LISTS KWIQ_HEADERS)
    string(MAKE_C_IDENTIFIER "${h}" n)
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/header_check/${n}.cpp CONTENT "#include \"${h}\"\n")
    list(APPEND KWIQ_HEADER_CHECKS ${CMAKE_CURRENT_BINARY_DIR}/header_check/${n}.cpp)
endforeach()
add_library(KWIQHeaderCheck OBJECT ${KWIQ_HEADER_CHECKS})
target_include_directories(KWIQHeaderCheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(KWIQHeaderCheck SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs)
target_compile_definitions(KWIQHeaderCheck PRIVATE ${KWIQ_DEFINITIONS})
target_compile_options(KWIQHeaderCheck PRIVATE /W4)
target_link_libraries(KWIQHeaderCheck PRIVATE nlohmann_json::nlohmann_json CURL::libcurl)   # their include paths

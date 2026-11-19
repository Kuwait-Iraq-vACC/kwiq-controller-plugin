cmake_minimum_required(VERSION 3.21)
project(KWIQPlugin LANGUAGES CXX)

# EuroScope plugins are 32-bit: configure with -A Win32 (see .github/workflows/build.yml).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")   # /MD, same as VCH

find_package(CURL CONFIG REQUIRED)               # from vcpkg (static, Windows schannel TLS)
find_package(nlohmann_json CONFIG REQUIRED)      # from vcpkg
find_package(ixwebsocket CONFIG REQUIRED)        # from vcpkg, with mbedtls: RDF (TrackAudio, ws://) and Core push events (wss://)
find_package(RapidJSON CONFIG REQUIRED)          # from vcpkg, header only: discord-rpc's JSON

# discord-rpc (Multi Theft Auto's fork, MIT), for DiscordEuroScope: built from libs/discord-rpc
add_library(discord-rpc STATIC
    libs/discord-rpc/discord_rpc.cpp
    libs/discord-rpc/rpc_connection.cpp
    libs/discord-rpc/serialization.cpp
    libs/discord-rpc/connection_win.cpp
    libs/discord-rpc/discord_register_win.cpp
)
target_include_directories(discord-rpc SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/discord-rpc)
# WIN32_LEAN_AND_MEAN empty, as connection_win.cpp and discord_register_win.cpp define it (no C4005)
target_compile_definitions(discord-rpc PRIVATE WIN32_LEAN_AND_MEAN= NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(discord-rpc PRIVATE rapidjson advapi32 psapi)

# Warnings (#41): KWIQ's own sources are built at /W4, and CI fails on any warning in them. Vendored code
# (VCH, the Slots-Plugin's VatsimAPI.cpp) keeps the default level. Headers from libs/ and vcpkg are external
# (SYSTEM, so /external:I and /external:W0), so their warnings never count.
set(KWIQ_OWN_SOURCES KWIQPlugin.cpp kwiq_ui.cpp keylink.cpp loader.cpp updater.cpp rdf.cpp discord.cpp push.cpp)
set_source_files_properties(${KWIQ_OWN_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4")
# WIN32_LEAN_AND_MEAN is empty, as VCH's stdafx.h defines it, so it is never redefined (C4005)
set(KWIQ_DEFINITIONS WIN32_LEAN_AND_MEAN= _CRT_SECURE_NO_WARNINGS)
# Set by GitHub Actions to its run number, so each build has its own version (0.6.0.42)
set(KWIQ_BUILD_NUMBER 0 CACHE STRING "Build number added to the plugin version")

# KWIQPluginCore.dll: the plugin itself
add_library(KWIQPluginCore SHARED
    KWIQPlugin.cpp
    kwiq_ui.cpp          # the windows: SRD Search, Event Routes, the API key setup box, Notifications
    VatsimAPI.cpp
    keylink.cpp          # receives the API key from myKWIQ
    kwiq.rc              # the SRD Search and Event Routes dialogs, and the KWIQ button's logo (kwiq_res.h)
    rdf.cpp              # RDF and the TrackAudio bridge (KingfuChan/RDF)
    discord.cpp          # DiscordEuroScope (Kirollos/DiscordEuroScope)
    push.cpp             # Core's push events (Laravel Reverb)
    requestMenu.cpp      # VCH
    internet.cpp         # VCH
    usefulStuff.cpp      # VCH
)

target_include_directories(KWIQPluginCore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(KWIQPluginCore SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs)
target_compile_definitions(KWIQPluginCore PRIVATE ${KWIQ_DEFINITIONS} KWIQ_BUILD_NUMBER=${KWIQ_BUILD_NUMBER})
target_link_libraries(KWIQPluginCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/EuroScopePlugInDll.lib
    CURL::libcurl
    nlohmann_json::nlohmann_json
    ixwebsocket::ixwebsocket                      # RDF: TrackAudio's WebSocket
    discord-rpc                                   # DiscordEuroScope
    crypt32 shlwapi bcrypt                        # ixwebsocket, mbedtls
    wininet                                       # VCH
    winmm                                         # PlaySound (sector file alert)
    ws2_32                                        # API key from myKWIQ (keylink.cpp)
    version                                       # GetFileVersionInfo (EuroScope version check)
    comctl32                                      # the SRD search and event routes lists
)
set_target_properties(KWIQPluginCore PROPERTIES PREFIX "" OUTPUT_NAME "KWIQPluginCore")

# The UK Controller Plugin's layout: the packages carry only KWIQPlugin.dll (the loader), which runs
# KWIQPluginUpdater.dll and then loads KWIQPluginCore.dll, both kept in %LOCALAPPDATA%\KWIQ\bin (loader.cpp)

# KWIQPluginUpdater.dll: keeps the plugin and itself up to date
add_library(KWIQPluginUpdater SHARED updater.cpp)
target_include_directories(KWIQPluginUpdater PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(KWIQPluginUpdater PRIVATE ${KWIQ_DEFINITIONS} NOMINMAX KWIQ_BUILD_NUMBER=${KWIQ_BUILD_NUMBER})
target_link_libraries(KWIQPluginUpdater PRIVATE winhttp shell32)
set_target_properties(KWIQPluginUpdater PROPERTIES PREFIX "" OUTPUT_NAME "KWIQPluginUpdater")

# KWIQPlugin.dll: the one file in the packages, loaded by EuroScope
add_library(KWIQPlugin SHARED loader.cpp)
target_include_directories(KWIQPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(KWIQPlugin SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs)
target_compile_definitions(KWIQPlugin PRIVATE ${KWIQ_DEFINITIONS} NOMINMAX KWIQ_BUILD_NUMBER=${KWIQ_BUILD_NUMBER})
target_link_libraries(KWIQPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/EuroScopePlugInDll.lib winhttp)
set_target_properties(KWIQPlugin PROPERTIES PREFIX "" OUTPUT_NAME "KWIQPlugin")

# Each KWIQ header must compile on its own (#41), e.g. windows.h before EuroScopePlugIn.h: one generated .cpp
# per header, built at /W4 with the plugin's definitions. Nothing is linked from it.
set(KWIQ_HEADERS kwiq_logic.hpp kwiq_win.hpp kwiq_ui.hpp keylink.hpp VatsimAPI.h version.h rdf.hpp discord.hpp push.hpp)
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

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

# Layout (after the UK Controller Plugin's):
#   src/plugin   KWIQPluginCore.dll, one folder per feature; vendored VCH (vch/) and Slots-Plugin (slots/) code
#   src/loader   KWIQPlugin.dll, the loader
#   src/updater  KWIQPluginUpdater.dll
#   src/utils    headers shared by all three DLLs and the tests
#   resource     kwiq.rc, its IDs and the logo images
#   third_party  the EuroScope SDK and discord-rpc
#   test         the logic tests
set(KWIQ_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(KWIQ_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(KWIQ_EUROSCOPE_LIB ${KWIQ_THIRD_PARTY}/euroscope/EuroScopePlugInDll.lib)
set(KWIQ_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)

# discord-rpc (Multi Theft Auto's fork, MIT), for DiscordEuroScope: built from third_party/discord-rpc
add_library(discord-rpc STATIC
    third_party/discord-rpc/discord_rpc.cpp
    third_party/discord-rpc/rpc_connection.cpp
    third_party/discord-rpc/serialization.cpp
    third_party/discord-rpc/connection_win.cpp
    third_party/discord-rpc/discord_register_win.cpp
)
target_include_directories(discord-rpc SYSTEM PUBLIC ${KWIQ_THIRD_PARTY}/discord-rpc)
# WIN32_LEAN_AND_MEAN empty, as connection_win.cpp and discord_register_win.cpp define it (no C4005)
target_compile_definitions(discord-rpc PRIVATE WIN32_LEAN_AND_MEAN= NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(discord-rpc PRIVATE rapidjson advapi32 psapi)

# Warnings (#41): KWIQ's own sources are built at /W4, and CI fails on any warning in them. Vendored code
# (src/plugin/vch, src/plugin/slots) keeps the default level. Headers from third_party/ and vcpkg are external
# (SYSTEM, so /external:I and /external:W0), so their warnings never count.
set(KWIQ_OWN_SOURCES
    src/plugin/KWIQPlugin.cpp
    src/plugin/ui/kwiq_ui.cpp
    src/plugin/keylink/keylink.cpp
    src/plugin/rdf/rdf.cpp
    src/plugin/discord/discord.cpp
    src/plugin/push/push.cpp
    src/loader/loader.cpp
    src/updater/updater.cpp
)
set_source_files_properties(${KWIQ_OWN_SOURCES} PROPERTIES COMPILE_OPTIONS "/W4")
# WIN32_LEAN_AND_MEAN is empty, as VCH's stdafx.h defines it, so it is never redefined (C4005)
set(KWIQ_DEFINITIONS WIN32_LEAN_AND_MEAN= _CRT_SECURE_NO_WARNINGS)
# The version is KWIQ_VERSION in src/utils/version.h (Semantic Versioning). GitHub Actions passes the version it builds
# (KWIQ_VERSION, or <version>-pr.<number>.<run> for a pull request) and build metadata (<run>.<commit>); they go
# into kwiq_build.h in the build folder, which version.h includes. A local build has neither.
set(KWIQ_BUILD_VERSION "" CACHE STRING "The SemVer version being built; empty: KWIQ_VERSION from version.h")
set(KWIQ_BUILD_METADATA "" CACHE STRING "SemVer build metadata, e.g. 115.a1b2c3d; never compared")
set(KWIQ_BUILD_H "// kwiq_build.h - written by CMakeLists.txt from what CI passes; see version.h\n")
if(KWIQ_BUILD_VERSION)
    string(APPEND KWIQ_BUILD_H "#define KWIQ_BUILD_VERSION \"${KWIQ_BUILD_VERSION}\"\n")
endif()
if(KWIQ_BUILD_METADATA)
    string(APPEND KWIQ_BUILD_H "#define KWIQ_BUILD_METADATA \"${KWIQ_BUILD_METADATA}\"\n")
endif()
file(CONFIGURE OUTPUT ${KWIQ_GENERATED}/kwiq_build.h CONTENT "${KWIQ_BUILD_H}" @ONLY)
# Not kept in the cache: each configure takes only what it's given, so a build folder configured once by hand
# with -DKWIQ_BUILD_VERSION doesn't keep that version
unset(KWIQ_BUILD_VERSION CACHE)
unset(KWIQ_BUILD_METADATA CACHE)

# KWIQPluginCore.dll: the plugin itself
add_library(KWIQPluginCore SHARED
    src/plugin/KWIQPlugin.cpp
    src/plugin/ui/kwiq_ui.cpp          # the windows: SRD Search, Event Routes, the API key setup box, Notifications
    src/plugin/slots/VatsimAPI.cpp     # slot detection (Slots-Plugin)
    src/plugin/keylink/keylink.cpp     # receives the API key from myKWIQ
    resource/kwiq.rc                   # the SRD Search and Event Routes dialogs, and the KWIQ button's logo (kwiq_res.h)
    src/plugin/rdf/rdf.cpp             # RDF and the TrackAudio bridge (KingfuChan/RDF)
    src/plugin/discord/discord.cpp     # DiscordEuroScope (Kirollos/DiscordEuroScope)
    src/plugin/push/push.cpp           # Core's push events (Laravel Reverb)
    src/plugin/vch/requestMenu.cpp     # VCH
    src/plugin/vch/usefulStuff.cpp     # VCH
)

# Includes are relative to src/plugin (e.g. "rdf/rdf.hpp") or src/utils, resource and the build's generated folder
target_include_directories(KWIQPluginCore PRIVATE ${KWIQ_SRC}/plugin ${KWIQ_SRC}/utils ${CMAKE_CURRENT_SOURCE_DIR}/resource ${KWIQ_GENERATED})
target_include_directories(KWIQPluginCore SYSTEM PRIVATE ${KWIQ_THIRD_PARTY}/euroscope)
target_compile_definitions(KWIQPluginCore PRIVATE ${KWIQ_DEFINITIONS})
target_link_libraries(KWIQPluginCore PRIVATE
    ${KWIQ_EUROSCOPE_LIB}
    CURL::libcurl
    nlohmann_json::nlohmann_json
    ixwebsocket::ixwebsocket                      # RDF: TrackAudio's WebSocket
    discord-rpc                                   # DiscordEuroScope
    crypt32 shlwapi bcrypt                        # ixwebsocket, mbedtls
    winmm                                         # PlaySound (sector file alert)
    ws2_32                                        # API key from myKWIQ (src/plugin/keylink/keylink.cpp)
    version                                       # GetFileVersionInfo (EuroScope version check)
    comctl32                                      # the SRD search and event routes lists
)
set_target_properties(KWIQPluginCore PROPERTIES PREFIX "" OUTPUT_NAME "KWIQPluginCore")

# The UK Controller Plugin's layout: the packages carry only KWIQPlugin.dll (the loader), which runs
# KWIQPluginUpdater.dll and then loads KWIQPluginCore.dll, both kept in %LOCALAPPDATA%\KWIQ\bin (src/loader/loader.cpp)

# KWIQPluginUpdater.dll: keeps the plugin and itself up to date
add_library(KWIQPluginUpdater SHARED src/updater/updater.cpp)
target_include_directories(KWIQPluginUpdater PRIVATE ${KWIQ_SRC}/utils ${KWIQ_GENERATED})
target_compile_definitions(KWIQPluginUpdater PRIVATE ${KWIQ_DEFINITIONS} NOMINMAX)
target_link_libraries(KWIQPluginUpdater PRIVATE winhttp shell32)
set_target_properties(KWIQPluginUpdater PROPERTIES PREFIX "" OUTPUT_NAME "KWIQPluginUpdater")

# KWIQPlugin.dll: the one file in the packages, loaded by EuroScope.
# KWIQPluginCore.dll is also built into it, as the KWIQCORE resource, for 0.5 copies alone: their loader
# updates by downloading the latest KWIQPlugin.dll from Core, taking the plugin out of that resource and
# replacing itself with it. So a 0.5 copy (0.5.0.56 in the packages, or 0.5.0.115) moves on to this loader
# without a new sector file. This loader never reads the resource. Keep it in every release: a 0.5 loader
# that could not replace itself (a read-only sector file folder) keeps updating this way.
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/core_$<CONFIG>.rc
     CONTENT "KWIQCORE RCDATA \"$<TARGET_FILE:KWIQPluginCore>\"
")
add_library(KWIQPlugin SHARED src/loader/loader.cpp ${CMAKE_CURRENT_BINARY_DIR}/core_$<CONFIG>.rc)
add_dependencies(KWIQPlugin KWIQPluginCore)
target_include_directories(KWIQPlugin PRIVATE ${KWIQ_SRC}/utils ${KWIQ_GENERATED})
target_include_directories(KWIQPlugin SYSTEM PRIVATE ${KWIQ_THIRD_PARTY}/euroscope)
target_compile_definitions(KWIQPlugin PRIVATE ${KWIQ_DEFINITIONS} NOMINMAX)
target_link_libraries(KWIQPlugin PRIVATE ${KWIQ_EUROSCOPE_LIB} winhttp)
set_target_properties(KWIQPlugin PROPERTIES PREFIX "" OUTPUT_NAME "KWIQPlugin")

# Each KWIQ header must compile on its own (#41), e.g. windows.h before EuroScopePlugIn.h: one generated .cpp
# per header, built at /W4 with the plugin's definitions. Nothing is linked from it.
# Paths as they are included: src/utils's headers by name, src/plugin's from src/plugin
set(KWIQ_HEADERS kwiq_logic.hpp kwiq_log.hpp kwiq_win.hpp version.h ownership/ownership_logic.hpp ui/kwiq_ui.hpp keylink/keylink.hpp slots/VatsimAPI.h rdf/rdf.hpp discord/discord.hpp push/push.hpp)
set(KWIQ_HEADER_CHECKS "")
foreach(h IN LISTS KWIQ_HEADERS)
    string(MAKE_C_IDENTIFIER "${h}" n)
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/header_check/${n}.cpp CONTENT "#include \"${h}\"\n")
    list(APPEND KWIQ_HEADER_CHECKS ${CMAKE_CURRENT_BINARY_DIR}/header_check/${n}.cpp)
endforeach()
add_library(KWIQHeaderCheck OBJECT ${KWIQ_HEADER_CHECKS})
target_include_directories(KWIQHeaderCheck PRIVATE ${KWIQ_SRC}/plugin ${KWIQ_SRC}/utils ${KWIQ_GENERATED})   # version.h as CI builds it
target_include_directories(KWIQHeaderCheck SYSTEM PRIVATE ${KWIQ_THIRD_PARTY}/euroscope)
target_compile_definitions(KWIQHeaderCheck PRIVATE ${KWIQ_DEFINITIONS})
target_compile_options(KWIQHeaderCheck PRIVATE /W4)
target_link_libraries(KWIQHeaderCheck PRIVATE nlohmann_json::nlohmann_json CURL::libcurl)   # their include paths

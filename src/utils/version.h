// version.h - the plugin's version, shared by the loader (loader.cpp), the updater (updater.cpp) and the plugin.
//
// KWIQ_VERSION is a Semantic Versioning 2.0.0 version (https://semver.org/): MAJOR.MINOR.PATCH, optionally with
// a -prerelease (0.7.0-rc.1). Change it by hand in the pull request that should make a release: a push to main
// publishes v<KWIQ_VERSION> once, only if that version isn't released yet (.github/workflows/build.yml).
//
// GitHub Actions passes two more things through kwiq_build.h, which CMakeLists.txt writes into the build folder
// (a local build has neither):
//   KWIQ_BUILD_VERSION   the version it builds: KWIQ_VERSION, or <version>-pr.<number>.<run> for a pull request
//   KWIQ_BUILD_METADATA  <run>.<commit>, e.g. 115.a1b2c3d
// KWIQ_PLUGIN_VERSION is what the plugin compares and sends to Core (kwiq::CompareVersions); KWIQ_PLUGIN_BUILD
// adds the build metadata (0.7.0+115.a1b2c3d) for .kwiq status and the log. Build metadata never counts when
// versions are compared. The DLLs have no Windows file version resource, so nothing else carries a version.
#pragma once

#define KWIQ_VERSION "0.7.3"

#if defined(__has_include)
#if __has_include("kwiq_build.h")
#include "kwiq_build.h"
#endif
#endif

#ifdef KWIQ_BUILD_VERSION
#define KWIQ_PLUGIN_VERSION KWIQ_BUILD_VERSION
#else
#define KWIQ_PLUGIN_VERSION KWIQ_VERSION
#endif

#ifdef KWIQ_BUILD_METADATA
#define KWIQ_PLUGIN_BUILD KWIQ_PLUGIN_VERSION "+" KWIQ_BUILD_METADATA
#else
#define KWIQ_PLUGIN_BUILD KWIQ_PLUGIN_VERSION
#endif

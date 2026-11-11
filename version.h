// version.h - the plugin's version, shared by the loader (loader.cpp), the updater (updater.cpp) and the plugin.
// Change the base version by hand; GitHub Actions adds its build number (0.6.0.42) to every release.
#pragma once

#define KWIQ_PLUGIN_VERSION_BASE "0.6.0"
#ifndef KWIQ_BUILD_NUMBER
#define KWIQ_BUILD_NUMBER 0
#endif
#define KWIQ_STR2(x) #x
#define KWIQ_STR(x) KWIQ_STR2(x)
#if KWIQ_BUILD_NUMBER > 0
#define KWIQ_PLUGIN_VERSION   KWIQ_PLUGIN_VERSION_BASE "." KWIQ_STR(KWIQ_BUILD_NUMBER)
#else
#define KWIQ_PLUGIN_VERSION   KWIQ_PLUGIN_VERSION_BASE
#endif

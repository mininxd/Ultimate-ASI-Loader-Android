#pragma once

#include <jni.h>
#include <string>
#include <vector>

#define ASI_LOADER_VERSION "1.0.0-Android"

#ifdef __cplusplus
extern "C" {
#endif

// Exported symbols for plugins and external inspection
JNIEXPORT jboolean JNICALL Java_com_rockstargames_gtasa_GTASA_isASILoaderActive(JNIEnv* env, jclass clazz);
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved);

bool IsUltimateASILoader(void);
const char* GetASILoaderVersion(void);
void ASILoader_Initialize(void);

#ifdef __cplusplus
}
#endif

namespace ual::android
{
    struct AndroidSettings
    {
        bool loadPlugins = true;
        bool loadFromScriptsOnly = false;
        bool loadRecursively = true;
        bool debugLog = true;
        std::string customPluginsDir = "";
        std::string extraPlugins = "";
    };

    void Log(const char* fmt, ...);
    const AndroidSettings& GetSettings();
}

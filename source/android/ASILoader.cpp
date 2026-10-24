#include "ASILoader.h"
#include "ini.h"

#include <android/log.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <mutex>

#define LOG_TAG "UltimateASILoader"

static JavaVM* g_vm = nullptr;
static std::mutex g_initMutex;
static bool g_initialized = false;
static FILE* g_logFile = nullptr;

namespace ual::android
{
    static AndroidSettings g_settings;
    static std::string g_packageName = "com.rockstargames.gtasa";
    static std::string g_internalDataDir = "/data/data/" + g_packageName;
    static std::string g_externalMediaDir = "/storage/emulated/0/Android/media/" + g_packageName;
    static std::string g_externalDataDir = "/storage/emulated/0/Android/data/" + g_packageName;
    static std::set<std::string> g_loadedPlugins;

    void Log(const char* fmt, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buffer);

        if (g_logFile)
        {
            fprintf(g_logFile, "[ASILoader] %s\n", buffer);
            fflush(g_logFile);
        }
    }

    const AndroidSettings& GetSettings()
    {
        return g_settings;
    }

    static bool EndsWithCaseInsensitive(const std::string& str, const std::string& suffix)
    {
        if (str.length() < suffix.length()) return false;
        size_t offset = str.length() - suffix.length();
        for (size_t i = 0; i < suffix.length(); ++i)
        {
            if (tolower(str[offset + i]) != tolower(suffix[i])) return false;
        }
        return true;
    }

    static bool DirectoryExists(const std::string& path)
    {
        struct stat st;
        return (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
    }

    static bool FileExists(const std::string& path)
    {
        struct stat st;
        return (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode));
    }

    static bool CreateDirectoryRecursive(const std::string& path)
    {
        if (path.empty() || DirectoryExists(path)) return true;

        size_t pos = 0;
        while ((pos = path.find('/', pos + 1)) != std::string::npos)
        {
            std::string sub = path.substr(0, pos);
            if (!sub.empty() && !DirectoryExists(sub))
            {
                mkdir(sub.c_str(), 0755);
            }
        }
        return mkdir(path.c_str(), 0755) == 0 || DirectoryExists(path);
    }

    static void DetectPackageName()
    {
        int fd = open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0)
        {
            char cmdline[256] = {0};
            ssize_t len = read(fd, cmdline, sizeof(cmdline) - 1);
            close(fd);
            if (len > 0)
            {
                cmdline[len] = '\0';
                // cmdline may contain process name up to null terminator or ':'
                char* colon = strchr(cmdline, ':');
                if (colon) *colon = '\0';
                if (strlen(cmdline) > 0)
                {
                    g_packageName = cmdline;
                }
            }
        }

        g_internalDataDir = "/data/data/" + g_packageName;
        if (!DirectoryExists(g_internalDataDir))
        {
            std::string user0 = "/data/user/0/" + g_packageName;
            if (DirectoryExists(user0))
            {
                g_internalDataDir = user0;
            }
        }

        g_externalMediaDir = "/storage/emulated/0/Android/media/" + g_packageName;
        if (!DirectoryExists(g_externalMediaDir))
        {
            std::string sdcardMedia = "/sdcard/Android/media/" + g_packageName;
            if (DirectoryExists(sdcardMedia))
            {
                g_externalMediaDir = sdcardMedia;
            }
        }

        g_externalDataDir = "/storage/emulated/0/Android/data/" + g_packageName;
        if (!DirectoryExists(g_externalDataDir))
        {
            std::string sdcard = "/sdcard/Android/data/" + g_packageName;
            if (DirectoryExists(sdcard))
            {
                g_externalDataDir = sdcard;
            }
        }
    }

    static void LoadIniSettings()
    {
        std::vector<std::string> searchBases = {
            g_externalMediaDir + "/files",
            g_externalMediaDir,
            g_externalDataDir + "/files",
            g_externalDataDir,
            g_internalDataDir + "/files",
            g_internalDataDir
        };

        std::vector<std::string> iniSubdirs = {
            "scripts", "plugins", "ASI", "asi", ""
        };

        for (const auto& base : searchBases)
        {
            for (const auto& sub : iniSubdirs)
            {
                std::string dir = sub.empty() ? base : (base + "/" + sub);
                std::string globalIni = dir + "/global.ini";
                std::string asiIni = dir + "/ASILoader.ini";

                IniReader reader;
                if (reader.LoadFile(globalIni) || reader.LoadFile(asiIni))
                {
                    g_settings.loadPlugins = reader.GetBoolean("GlobalSets", "LoadPlugins", g_settings.loadPlugins);
                    g_settings.loadFromScriptsOnly = reader.GetBoolean("GlobalSets", "LoadFromScriptsOnly", g_settings.loadFromScriptsOnly);
                    g_settings.loadRecursively = reader.GetBoolean("GlobalSets", "LoadRecursively", g_settings.loadRecursively);
                    g_settings.debugLog = reader.GetBoolean("GlobalSets", "DebugLog", g_settings.debugLog);
                    g_settings.extraPlugins = reader.GetString("GlobalSets", "LoadExtraPlugins", g_settings.extraPlugins);
                    Log("Loaded configuration from %s", FileExists(globalIni) ? globalIni.c_str() : asiIni.c_str());
                    return;
                }
            }
        }
    }

    static bool CopyFileInternal(const std::string& src, const std::string& dst)
    {
        struct stat stSrc;
        if (stat(src.c_str(), &stSrc) != 0) return false;

        struct stat stDst;
        if (stat(dst.c_str(), &stDst) == 0)
        {
            if (stDst.st_size == stSrc.st_size && stDst.st_mtime >= stSrc.st_mtime)
            {
                return true; // Already up to date
            }
        }

        int inFd = open(src.c_str(), O_RDONLY);
        if (inFd < 0) return false;

        int outFd = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (outFd < 0)
        {
            close(inFd);
            return false;
        }

        char buffer[65536];
        ssize_t bytes;
        bool success = true;

        while ((bytes = read(inFd, buffer, sizeof(buffer))) > 0)
        {
            if (write(outFd, buffer, bytes) != bytes)
            {
                success = false;
                break;
            }
        }

        close(inFd);
        close(outFd);

        if (success)
        {
            chmod(dst.c_str(), 0755);
        }
        return success;
    }

    static bool IsExcludedLibrary(const std::string& filename)
    {
        static const char* excluded[] = {
            "libASILoader.so",
            "libUltimateASILoader.so",
            "libGTASA.so",
            "libAML.so",
            "libSCAnd.so",
            "libImmEmulatorJ.so",
            "libTouchSenseSDK.so",
            "libcleo.so",
            "libthreadfix.so",
            "libFastman92pluginLoader_ANDROID_ARM32.so",
            "libplugin_fastman92limitAdjuster_ANDROID_ARM32.so",
            "libpluginCleoExtender.so",
            "libc.so",
            "libm.so",
            "libdl.so",
            "liblog.so",
            "libstdc++.so",
            "libandroid.so",
            "libEGL.so",
            "libGLESv2.so",
            "libOpenSLES.so"
        };

        for (const char* name : excluded)
        {
            if (strcasecmp(filename.c_str(), name) == 0) return true;
        }
        return false;
    }

    static void LoadPluginFile(const std::string& filepath, const std::string& filename)
    {
        if (IsExcludedLibrary(filename)) return;

        if (g_loadedPlugins.find(filename) != g_loadedPlugins.end())
        {
            Log("Plugin %s already loaded, skipping.", filename.c_str());
            return;
        }

        std::string finalPath = filepath;

        // Android external storage (/sdcard) is mounted noexec.
        // Dynamic linker dlopen will fail with EACCES unless the file is copied to private app storage.
        if (filepath.rfind("/storage/", 0) == 0 || filepath.rfind("/sdcard/", 0) == 0)
        {
            std::string execDir = g_internalDataDir + "/files/asi_plugins";
            CreateDirectoryRecursive(execDir);

            std::string execPath = execDir + "/" + filename;
            Log("Copying external plugin %s -> %s for execution permissions", filepath.c_str(), execPath.c_str());
            if (CopyFileInternal(filepath, execPath))
            {
                finalPath = execPath;
            }
            else
            {
                Log("Failed to copy %s to %s", filepath.c_str(), execPath.c_str());
                return;
            }
        }
        else
        {
            chmod(finalPath.c_str(), 0755);
        }

        Log("Loading plugin: %s", finalPath.c_str());
        void* handle = dlopen(finalPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!handle)
        {
            const char* err = dlerror();
            Log("Failed to load %s: %s", finalPath.c_str(), err ? err : "unknown error");
            return;
        }

        g_loadedPlugins.insert(filename);
        Log("Successfully loaded plugin: %s (handle: %p)", filename.c_str(), handle);

        // 1. Classic ASI Loader initialization function
        typedef void (*InitializeASI_t)(void);
        InitializeASI_t initAsi = (InitializeASI_t)dlsym(handle, "InitializeASI");
        if (initAsi)
        {
            Log("Invoking InitializeASI() in %s", filename.c_str());
            initAsi();
        }

        // 2. Android JNI_OnLoad if present
        typedef jint (*JNI_OnLoad_t)(JavaVM*, void*);
        JNI_OnLoad_t jniOnLoad = (JNI_OnLoad_t)dlsym(handle, "JNI_OnLoad");
        if (jniOnLoad && g_vm)
        {
            Log("Invoking JNI_OnLoad() in %s", filename.c_str());
            jniOnLoad(g_vm, nullptr);
        }
    }

    static void ScanAndLoadDirectory(const std::string& dirPath, bool recursive)
    {
        if (!DirectoryExists(dirPath)) return;

        DIR* dir = opendir(dirPath.c_str());
        if (!dir) return;

        Log("Scanning directory for ASI/SO plugins: %s", dirPath.c_str());

        std::vector<std::string> subdirs;
        std::vector<std::pair<std::string, std::string>> pluginsToLoad;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            std::string fullPath = dirPath + "/" + entry->d_name;
            struct stat st;
            if (stat(fullPath.c_str(), &st) != 0) continue;

            if (S_ISDIR(st.st_mode))
            {
                if (recursive)
                {
                    subdirs.push_back(fullPath);
                }
            }
            else if (S_ISREG(st.st_mode))
            {
                std::string fname = entry->d_name;
                if (EndsWithCaseInsensitive(fname, ".asi") || EndsWithCaseInsensitive(fname, ".so"))
                {
                    pluginsToLoad.emplace_back(fullPath, fname);
                }
            }
        }
        closedir(dir);

        // Load direct plugins first
        for (const auto& item : pluginsToLoad)
        {
            LoadPluginFile(item.first, item.second);
        }

        // Recurse subdirectories if enabled
        for (const auto& sub : subdirs)
        {
            ScanAndLoadDirectory(sub, recursive);
        }
    }
}

extern "C" {

bool IsUltimateASILoader(void)
{
    return true;
}

const char* GetASILoaderVersion(void)
{
    return ASI_LOADER_VERSION;
}

JNIEXPORT jboolean JNICALL Java_com_rockstargames_gtasa_GTASA_isASILoaderActive(JNIEnv* env, jclass clazz)
{
    return JNI_TRUE;
}

void ASILoader_Initialize(void)
{
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_initialized) return;
    g_initialized = true;

    ual::android::DetectPackageName();

    // Setup file logger in app files directory
    std::string logFilePath = ual::android::g_internalDataDir + "/files/ASILoader.log";
    ual::android::CreateDirectoryRecursive(ual::android::g_internalDataDir + "/files");
    g_logFile = fopen(logFilePath.c_str(), "w");

    ual::android::Log("=================================================");
    ual::android::Log("Ultimate ASI Loader for Android v%s", ASI_LOADER_VERSION);
    ual::android::Log("Target Package: %s", ual::android::g_packageName.c_str());
    ual::android::Log("Internal Dir:       %s", ual::android::g_internalDataDir.c_str());
    ual::android::Log("External Media Dir: %s", ual::android::g_externalMediaDir.c_str());
    ual::android::Log("External Data Dir:  %s", ual::android::g_externalDataDir.c_str());
#if defined(__aarch64__)
    ual::android::Log("Architecture:   ARM64 (arm64-v8a)");
#elif defined(__arm__)
    ual::android::Log("Architecture:   ARM32 (armeabi-v7a)");
#elif defined(__x86_64__)
    ual::android::Log("Architecture:   x86_64");
#elif defined(__i386__)
    ual::android::Log("Architecture:   x86");
#else
    ual::android::Log("Architecture:   Unknown");
#endif
    ual::android::Log("=================================================");

    ual::android::LoadIniSettings();

    if (!ual::android::g_settings.loadPlugins)
    {
        ual::android::Log("LoadPlugins is set to 0. Plugin loading aborted.");
        return;
    }

    // Search folders (Android/media prioritized over Android/data)
    std::vector<std::string> searchBases = {
        ual::android::g_externalMediaDir + "/files",
        ual::android::g_externalMediaDir,
        ual::android::g_externalDataDir + "/files",
        ual::android::g_externalDataDir,
        ual::android::g_internalDataDir + "/files",
        ual::android::g_internalDataDir
    };

    std::vector<std::string> pluginSubdirs = {
        "scripts", "plugins", "ASI", "asi"
    };

    // 1. Load from scripts / plugins / ASI subdirectories
    for (const auto& base : searchBases)
    {
        for (const auto& sub : pluginSubdirs)
        {
            std::string targetDir = base + "/" + sub;
            ual::android::ScanAndLoadDirectory(targetDir, ual::android::g_settings.loadRecursively);
        }
    }

    // 2. If LoadFromScriptsOnly is false, also scan the root files directory
    if (!ual::android::g_settings.loadFromScriptsOnly)
    {
        for (const auto& base : searchBases)
        {
            ual::android::ScanAndLoadDirectory(base, false);
        }
    }

    ual::android::Log("ASI Loader initialization complete. Total plugins loaded: %zu", ual::android::g_loadedPlugins.size());
}

// Automatically runs upon dlopen or library load
__attribute__((constructor)) void ASILoader_InitConstructor(void)
{
    ASILoader_Initialize();
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
    g_vm = vm;
    ASILoader_Initialize();
    return JNI_VERSION_1_6;
}

}

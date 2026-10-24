# Ultimate ASI Loader for Android (GTA SA & Native Apps)

This is an Android native port of **Ultimate ASI Loader** that adds `.asi` and `.so` plugin loading capabilities to Android applications like Grand Theft Auto: San Andreas (and other native Android apps).

It supports both **ARM 32-bit (`armeabi-v7a`)** and **ARM 64-bit (`arm64-v8a`)**.

---

## Architecture & How It Works

### 1. The `Android/media/` Path Redirection
On Android 11, 12, 13, and 14+, Google's Scoped Storage restricts access to `Android/data/com.rockstargames.gtasa/`. File managers and users are blocked from modifying files in that directory without root or Shizuku.

To solve this, the APK is patched in [`WarMedia.smali`](file:///gtasa_unpacked/smali/com/wardrumstudios/utils/WarMedia.smali) and [`NDKHelper.smali`](file:///gtasa_unpacked/smali/com/wardrumstudios/utils/NDKHelper.smali):
- All game storage operations redirect from `Android/data/` to **`Android/media/com.rockstargames.gtasa/files/`**.
- The `Android/media/` directory is fully accessible to users, file managers, and PC transfers over USB.
- The loader automatically creates the directory if it does not exist yet.

### 2. The Android Storage Execution Restriction & Bypass
On modern Android, external storage is mounted with `noexec` flags under SELinux. Calling `dlopen()` directly on any `.asi` or `.so` located in external storage fails with `Permission denied`.

To allow modders to push `.asi` or `.so` files to external storage without root or repacking every time:
- The loader scans public folders (e.g. `/sdcard/Android/data/com.rockstargames.gtasa/files/scripts/` or `plugins/` or `ASI/`).
- When a plugin is detected, it automatically copies the file into the application's private internal storage (`/data/data/com.rockstargames.gtasa/files/asi_plugins/`) where execution permissions are granted.
- It sets permissions (`chmod 0755`) and performs `dlopen()` with `RTLD_NOW | RTLD_GLOBAL`.

### 2. Plugin Entry Points
When a plugin is loaded:
1. Native constructors (`__attribute__((constructor))`) run automatically.
2. If the plugin exports `void InitializeASI(void)`, the loader calls it.
3. If the plugin exports `jint JNI_OnLoad(JavaVM* vm, void* reserved)`, the loader calls it with the active JVM instance.

### 3. Configuration (`global.ini` / `ASILoader.ini`)
Place `global.ini` or `ASILoader.ini` in the `scripts/` or `files/` folder:
```ini
[GlobalSets]
LoadPlugins = 1          ; 1 = enabled, 0 = disabled
LoadFromScriptsOnly = 0  ; 1 = only scripts/ and plugins/ folders, 0 = also root files dir
LoadRecursively = 1      ; 1 = search subdirectories
DebugLog = 1             ; 1 = detailed logs to logcat and ASILoader.log
```

---

## Building the `.so` Files (GitHub Actions)

Per your instructions, compilation of `.so` files is **never done locally**, but executed via GitHub Actions to ensure clean, reproducible builds across NDK environments.

The workflow file is located at [`.github/workflows/sobuild.yml`](file:///.github/workflows/sobuild.yml).

### To Trigger the Build:
1. Push your changes to GitHub:
   ```bash
   git add .
   git commit -m "Add Android ASI Loader support"
   git push origin master
   ```
2. Navigate to **Actions** in your GitHub repository.
3. Select **Build Android .so Libraries**.
4. Click **Run workflow** (or let it run automatically on push).
5. Once complete, download the artifact named **`Ultimate-ASI-Loader-Android-libs`**.
   The zip contains:
   - `armeabi-v7a/libASILoader.so`
   - `arm64-v8a/libASILoader.so`

---

## Patching into `gtasa.apk`

We have unpacked `gtasa.apk` into [`gtasa_unpacked/`](file:///gtasa_unpacked/) and patched `com/rockstargames/gtasa/GTASA.smali` to load `ASILoader` on boot.

Once you have downloaded the built `.so` artifact from GitHub Actions, run:

```bash
# Option A: Point to the extracted dist folder
./scripts/patch_apk.sh dist/

# Option B: Point directly to the 32-bit library
./scripts/patch_apk.sh dist/armeabi-v7a/libASILoader.so
```

This will:
1. Copy `libASILoader.so` into `gtasa_unpacked/lib/armeabi-v7a/` and `arm64-v8a/`.
2. Ensure `GTASA.smali` has the `System.loadLibrary("ASILoader")` hook.
3. Build the APK using `apktool`.
4. Sign the APK with a debug keystore.
5. Produce `gtasa-patched-signed.apk` ready to install on your Android device!

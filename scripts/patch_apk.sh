#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
UNPACKED_DIR="${ROOT_DIR}/gtasa_unpacked"
OUTPUT_APK="${ROOT_DIR}/gtasa-patched-signed.apk"
KEYSTORE="${ROOT_DIR}/release.jks"
KEY_ALIAS="gtasa"
KEY_PASS="gtasa123"

echo "================================================="
echo "   GTA: SA Android ASI Loader APK Patcher        "
echo "================================================="

SO_INPUT="$1"

if [ ! -d "$UNPACKED_DIR" ]; then
    echo "Error: Unpacked directory '$UNPACKED_DIR' not found!"
    echo "Please unpack gtasa.apk first using: apktool d -r gtasa.apk -o gtasa_unpacked"
    exit 1
fi

mkdir -p "${UNPACKED_DIR}/lib/armeabi-v7a"
mkdir -p "${UNPACKED_DIR}/lib/arm64-v8a"

if [ -n "$SO_INPUT" ] && [ "$SO_INPUT" != "--build-only" ]; then
    if [ -d "$SO_INPUT" ]; then
        if [ -f "${SO_INPUT}/armeabi-v7a/libASILoader.so" ]; then
            echo "Installing armeabi-v7a library..."
            cp "${SO_INPUT}/armeabi-v7a/libASILoader.so" "${UNPACKED_DIR}/lib/armeabi-v7a/"
        fi
        if [ -f "${SO_INPUT}/arm64-v8a/libASILoader.so" ]; then
            echo "Installing arm64-v8a library..."
            cp "${SO_INPUT}/arm64-v8a/libASILoader.so" "${UNPACKED_DIR}/lib/arm64-v8a/"
        fi
    elif [ -f "$SO_INPUT" ]; then
        echo "Installing $SO_INPUT into armeabi-v7a..."
        cp "$SO_INPUT" "${UNPACKED_DIR}/lib/armeabi-v7a/libASILoader.so"
    else
        echo "Error: File or directory '$SO_INPUT' does not exist."
        exit 1
    fi
else
    echo "No .so specified or --build-only passed. Rebuilding existing unpacked state..."
fi

SMALI_FILE="${UNPACKED_DIR}/smali/com/rockstargames/gtasa/GTASA.smali"
if [ -f "$SMALI_FILE" ]; then
    if grep -q "ASILoader" "$SMALI_FILE"; then
        echo "Smali hook already present in GTASA.smali."
    else
        echo "Patching GTASA.smali to load libASILoader.so..."
        python3 - << 'EOF'
import sys

smali_path = "gtasa_unpacked/smali/com/rockstargames/gtasa/GTASA.smali"
with open(smali_path, "r", encoding="utf-8") as f:
    content = f.read()

target = '    const-string v0, "AML"\n\n    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n\n    .line 457\n    return-void'
replacement = '''    const-string v0, "AML"\n\n    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n\n    :try_start_asi\n    const-string v0, "ASILoader"\n\n    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n    :try_end_asi\n    .catch Ljava/lang/Throwable; {:try_start_asi .. :try_end_asi} :catch_asi\n\n    :catch_asi\n    .line 457\n    return-void'''

if target in content:
    content = content.replace(target, replacement, 1)
    with open(smali_path, "w", encoding="utf-8") as f:
        f.write(content)
    print("Successfully patched GTASA.smali!")
else:
    print("Warning: Target pattern not found in GTASA.smali. It may already be patched or has a different structure.")
EOF
    fi
fi

echo "Building patched APK with apktool..."
UNSIGNED_APK="${ROOT_DIR}/gtasa-patched-unsigned.apk"
rm -f "$UNSIGNED_APK" "$OUTPUT_APK"
apktool b "$UNPACKED_DIR" -o "$UNSIGNED_APK"

echo "Signing APK with release JKS keystore..."
if [ ! -f "$KEYSTORE" ]; then
    echo "Generating release JKS keystore ($KEYSTORE)..."
    keytool -genkeypair -v -keystore "$KEYSTORE" -storepass "$KEY_PASS" -alias "$KEY_ALIAS" -keypass "$KEY_PASS" -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=GTASA Patch,OU=Gaming,O=Rockstar Modding,L=Los Santos,ST=San Andreas,C=US"
fi

if command -v zipalign >/dev/null 2>&1; then
    echo "Zipaligning APK..."
    ALIGNED_APK="${ROOT_DIR}/gtasa-aligned.apk"
    rm -f "$ALIGNED_APK"
    zipalign -p -f 4 "$UNSIGNED_APK" "$ALIGNED_APK"
    mv "$ALIGNED_APK" "$UNSIGNED_APK"
fi

if command -v apksigner >/dev/null 2>&1; then
    echo "Signing with apksigner (v1/v2/v3 scheme)..."
    cp "$UNSIGNED_APK" "$OUTPUT_APK"
    apksigner sign --ks "$KEYSTORE" --ks-pass "pass:$KEY_PASS" --ks-key-alias "$KEY_ALIAS" --key-pass "pass:$KEY_PASS" "$OUTPUT_APK"
else
    echo "Signing with jarsigner..."
    jarsigner -sigalg SHA256withRSA -digestalg SHA-256 -keystore "$KEYSTORE" -storepass "$KEY_PASS" -keypass "$KEY_PASS" -signedjar "$OUTPUT_APK" "$UNSIGNED_APK" "$KEY_ALIAS"
fi

rm -f "$UNSIGNED_APK"

if [ -d "/sdcard/cache" ]; then
    echo "Copying patched APK and JKS to /sdcard/cache..."
    cp "$OUTPUT_APK" "/sdcard/cache/gtasa-patched-signed.apk"
    cp "$KEYSTORE" "/sdcard/cache/" 2>/dev/null || true
fi

echo "================================================="
echo "Success! Patched and signed APK created at:"
echo "  $OUTPUT_APK"
echo "================================================="

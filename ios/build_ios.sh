#!/bin/sh
set -eu

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build-ios"}
CONFIG=${CONFIG:-Release}
IOS_VERSION=${IOS_VERSION:-1.2.5}
IOS_BUILD=${IOS_BUILD:-20}
OUTPUT_IPA=${OUTPUT_IPA:-"$ROOT_DIR/build-ios/th06-ios-${IOS_VERSION}-${IOS_BUILD}.ipa"}
BGM_DIR=${BGM_DIR:-"$ROOT_DIR/ios/bgm"}

if [ -z "${ASSET_APK:-}" ] && [ -z "${ASSET_DIR:-}" ]; then
    ASSET_DIR="$ROOT_DIR/ios/assets"
fi
if [ -n "${ASSET_APK:-}" ] && [ -n "${ASSET_DIR:-}" ]; then
    echo "error: set only one of ASSET_APK or ASSET_DIR" >&2
    exit 2
fi

python3 "$ROOT_DIR/ios/check_ios_source.py"

# Release builds start clean so stale Xcode objects and app resources cannot
# produce an IPA that differs from the extracted source package.
if [ "${CLEAN_BUILD:-1}" = "1" ]; then
    echo "Cleaning stale iOS build directory: $BUILD_DIR"
    cmake -E rm -rf "$BUILD_DIR"
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
    -DTH06_IOS_VERSION="$IOS_VERSION" \
    -DTH06_IOS_BUILD="$IOS_BUILD" \
    -DTH06_USE_VULKAN=OFF \
    -DTH06_ENABLE_PREDICTION_ROLLBACK=OFF

cmake --build "$BUILD_DIR" --config "$CONFIG" --target th06
APP_PATH="$BUILD_DIR/$CONFIG-iphoneos/th06.app"

if [ ! -f "$APP_PATH/Assets.car" ]; then
    echo "warning: Xcode did not compile ios/Assets.xcassets; compiling it explicitly" >&2
    xcrun actool \
        --output-format human-readable-text \
        --notices \
        --warnings \
        --platform iphoneos \
        --target-device ipad \
        --minimum-deployment-target 14.0 \
        --app-icon AppIcon \
        --output-partial-info-plist "$BUILD_DIR/assetcatalog-info.plist" \
        --compile "$APP_PATH" \
        "$ROOT_DIR/ios/Assets.xcassets"
fi
if [ ! -f "$APP_PATH/Assets.car" ]; then
    echo "error: icon asset compilation failed (Assets.car missing)" >&2
    exit 3
fi

if [ -n "${ASSET_APK:-}" ]; then
    python3 "$ROOT_DIR/ios/stage_assets.py" --output "$APP_PATH" --apk "$ASSET_APK" --bgm "$BGM_DIR"
else
    python3 "$ROOT_DIR/ios/stage_assets.py" --output "$APP_PATH" --assets "$ASSET_DIR" --bgm "$BGM_DIR"
fi
codesign --force --deep --sign - "$APP_PATH"

if ! strings "$APP_PATH/th06" | grep -Fq "netplay-ui=3.9.0"; then
    echo "error: built executable does not contain the 3.9.0 source marker" >&2
    exit 4
fi

PLIST_VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP_PATH/Info.plist")
PLIST_BUILD=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$APP_PATH/Info.plist")
if [ "$PLIST_VERSION" != "$IOS_VERSION" ] || [ "$PLIST_BUILD" != "$IOS_BUILD" ]; then
    echo "error: app version mismatch: expected $IOS_VERSION ($IOS_BUILD), got $PLIST_VERSION ($PLIST_BUILD)" >&2
    exit 5
fi

if ! /usr/libexec/PlistBuddy -c 'Print :NSBonjourServices:0' "$APP_PATH/Info.plist" | grep -Fq '_th06-netplay._udp'; then
    echo "error: built app is missing the Bonjour local-network permission trigger" >&2
    exit 6
fi
if ! /usr/libexec/PlistBuddy -c 'Print :NSBonjourServices:1' "$APP_PATH/Info.plist" | grep -Fq '_th06-peer._tcp'; then
    echo "error: built app is missing the nearby-device service declaration" >&2
    exit 7
fi
if ! /usr/libexec/PlistBuddy -c 'Print :NSBluetoothAlwaysUsageDescription' "$APP_PATH/Info.plist" >/dev/null; then
    echo "error: built app is missing the Bluetooth usage description" >&2
    exit 8
fi

echo "Verified package version $PLIST_VERSION ($PLIST_BUILD), netplay UI 3.9.0, nearby transports"
"$ROOT_DIR/ios/package_ipa.sh" "$APP_PATH" "$OUTPUT_IPA"

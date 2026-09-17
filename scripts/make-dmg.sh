#!/bin/bash
# Builds a universal (Apple Silicon + Intel) release and packages it as dist/Butterfader-<version>.dmg,
# containing a double-click installer (Install Butterfader.pkg).
#
# SKIP_BUILD=1 ARTEFACTS=build/Butterfader_artefacts/Release ./scripts/make-dmg.sh
#   packages an existing build instead (handy for testing the installer quickly).
#
# Optional environment variables (see README "Signing and notarizing the release"):
#   APP_SIGN_ID         "Developer ID Application: Name (TEAMID)" — signs the plug-ins and the DMG
#   INSTALLER_SIGN_ID   "Developer ID Installer: Name (TEAMID)"   — signs the .pkg
#   NOTARY_PROFILE      keychain profile from `xcrun notarytool store-credentials`
# Without them the build is ad-hoc signed: it works, but Gatekeeper makes users right-click > Open.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION="$(sed -n 's/^project(Butterfader VERSION \([0-9.]*\)).*/\1/p' CMakeLists.txt)"
BUILD="$ROOT/build-release"
ARTEFACTS="${ARTEFACTS:-$BUILD/Butterfader_artefacts/Release}"
WORK="$BUILD/package"
DMG="$ROOT/dist/Butterfader-$VERSION.dmg"

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    echo "==> Building Butterfader $VERSION (universal)"
    cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DBUTTERFADER_COPY_PLUGINS=OFF
    cmake --build "$BUILD" --config Release --target Butterfader_VST3 Butterfader_AU -j"$(sysctl -n hw.ncpu)"
fi

rm -rf "$WORK" && mkdir -p "$WORK"

if [[ -n "${APP_SIGN_ID:-}" ]]; then
    echo "==> Signing with Developer ID"
    for bundle in "$ARTEFACTS/VST3/Butterfader.vst3" "$ARTEFACTS/AU/Butterfader.component"; do
        # Hardened runtime + a secure timestamp: both are required for notarization.
        codesign --force --options runtime --timestamp --sign "$APP_SIGN_ID" "$bundle"
    done
else
    echo "==> Signing (ad-hoc — set APP_SIGN_ID for a Developer ID build)"
    for bundle in "$ARTEFACTS/VST3/Butterfader.vst3" "$ARTEFACTS/AU/Butterfader.component"; do
        codesign --force --sign - "$bundle"
    done
fi

echo "==> Building installer packages"
# One component package per format: <name> <bundle> <install location>
make_component () {
    local name="$1" bundle="$2" location="$3"
    local root="$WORK/roots/$name"
    mkdir -p "$root"
    cp -R "$bundle" "$root/"

    pkgbuild --analyze --root "$root" "$WORK/$name.plist" >/dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$name.plist"

    local scripts=()
    [[ "$name" == "au" ]] && scripts=(--scripts "$ROOT/packaging/scripts")

    pkgbuild --root "$root" \
             --component-plist "$WORK/$name.plist" \
             --install-location "$location" \
             --identifier "com.butterfader.butterfader.$name" \
             --version "$VERSION" \
             ${scripts[@]+"${scripts[@]}"} \
             "$WORK/pkgs/Butterfader-$name.pkg" >/dev/null
}
mkdir -p "$WORK/pkgs"
make_component vst3       "$ARTEFACTS/VST3/Butterfader.vst3"        "/Library/Audio/Plug-Ins/VST3"
make_component au         "$ARTEFACTS/AU/Butterfader.component"     "/Library/Audio/Plug-Ins/Components"

sed "s/@VERSION@/$VERSION/g" "$ROOT/packaging/distribution.xml" > "$WORK/distribution.xml"

STAGE="$WORK/dmg"
mkdir -p "$STAGE"
PKG="$STAGE/Install Butterfader.pkg"
PKG_SIGN_ARGS=()
[[ -n "${INSTALLER_SIGN_ID:-}" ]] && PKG_SIGN_ARGS=(--sign "$INSTALLER_SIGN_ID")
productbuild --distribution "$WORK/distribution.xml" \
             --resources "$ROOT/packaging/resources" \
             --package-path "$WORK/pkgs" \
             ${PKG_SIGN_ARGS[@]+"${PKG_SIGN_ARGS[@]}"} \
             "$PKG" >/dev/null
cp "$ROOT/packaging/READ ME FIRST.txt" "$STAGE/"

# Notarizing the .pkg before it goes in the DMG means it also opens on its own.
if [[ -n "${NOTARY_PROFILE:-}" && -n "${INSTALLER_SIGN_ID:-}" ]]; then
    echo "==> Notarizing installer (Apple takes a few minutes)"
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
fi

echo "==> Creating DMG"
mkdir -p "$ROOT/dist"
rm -f "$DMG"
hdiutil create -volname "Butterfader $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null

if [[ -n "${APP_SIGN_ID:-}" ]]; then
    codesign --force --timestamp --sign "$APP_SIGN_ID" "$DMG"
    if [[ -n "${NOTARY_PROFILE:-}" ]]; then
        echo "==> Notarizing disk image"
        xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
        # Stapling the ticket means the DMG validates with no network.
        xcrun stapler staple "$DMG"
        spctl -a -vvv -t install "$DMG" || true
    fi
fi

echo "==> Done: $DMG"

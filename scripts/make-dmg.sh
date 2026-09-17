#!/bin/bash
# Builds a universal (Apple Silicon + Intel) release and packages it as dist/Butterfader-<version>.dmg,
# containing a double-click installer (Install Butterfader.pkg).
#
# SKIP_BUILD=1 ARTEFACTS=build/Butterfader_artefacts/Release ./scripts/make-dmg.sh
#   packages an existing build instead (handy for testing the installer quickly).
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

echo "==> Signing (ad-hoc)"
for bundle in "$ARTEFACTS/VST3/Butterfader.vst3" "$ARTEFACTS/AU/Butterfader.component"; do
    codesign --force --deep --sign - "$bundle"
done

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
productbuild --distribution "$WORK/distribution.xml" \
             --resources "$ROOT/packaging/resources" \
             --package-path "$WORK/pkgs" \
             "$STAGE/Install Butterfader.pkg" >/dev/null
cp "$ROOT/packaging/READ ME FIRST.txt" "$STAGE/"

echo "==> Creating DMG"
mkdir -p "$ROOT/dist"
rm -f "$DMG"
hdiutil create -volname "Butterfader $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
echo "==> Done: $DMG"

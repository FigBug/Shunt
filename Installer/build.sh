#!/bin/bash -e
set -x

cd "$(dirname "$0")"
cd ..
PROJECT_ROOT=$(pwd)
GAME="Shunt"
VENDOR="SocaLabs"
BUNDLE_BASE="com.socalabs.shunt"
EDITOR="ShuntEditor"              # CMake target
EDITOR_APP="Shunt Level Editor"   # PRODUCT_NAME / bundle & exe base name

if [ "$(uname)" = "Darwin" ]; then
  PLATFORM="macOS"
elif [ "$(expr substr "$(uname -s)" 1 5)" = "Linux" ]; then
  PLATFORM="linux"
else
  PLATFORM="win"
fi

VERSION=$(cat "$PROJECT_ROOT/VERSION")

#
# Reset staging
#
rm -Rf "$PROJECT_ROOT/Installer/$PLATFORM/bin"
mkdir -p "$PROJECT_ROOT/Installer/$PLATFORM/bin"
rm -Rf "$PROJECT_ROOT/bin"
mkdir -p "$PROJECT_ROOT/bin"

############################################################
# macOS — pkgbuild + productbuild, install to /Applications
############################################################
if [ "$PLATFORM" = "macOS" ]; then
  TEAM_ID="${TEAM_ID:-3FS7DJDG38}"
  DEV_APP_ID="${DEV_APP_ID:-Developer ID Application: Roland Rabien (${TEAM_ID})}"
  DEV_INST_ID="${DEV_INST_ID:-Developer ID Installer: Roland Rabien (${TEAM_ID})}"

  #
  # Bootstrap a temp keychain from base64-encoded p12 secrets when running in CI.
  #
  if [ -n "${APPLICATION:-}" ] && [ -n "${INSTALLER:-}" ]; then
    KC_PASS="${KEYCHAIN_PASSWORD:-nr4aGPyz}"
    P12_PASS="${P12_PASSWORD:-aym9PKWB}"

    security create-keychain -p "$KC_PASS" Keys.keychain || true

    echo "$APPLICATION" | base64 -D -o /tmp/Application.p12
    echo "$INSTALLER"   | base64 -D -o /tmp/Installer.p12

    security import /tmp/Application.p12 -t agg -k Keys.keychain -P "$P12_PASS" -A -T /usr/bin/codesign
    security import /tmp/Installer.p12   -t agg -k Keys.keychain -P "$P12_PASS" -A -T /usr/bin/productsign

    security list-keychains -s Keys.keychain
    security default-keychain -s Keys.keychain
    security unlock-keychain -p "$KC_PASS" Keys.keychain
    security set-keychain-settings -l -u -t 13600 Keys.keychain
    security set-key-partition-list -S apple-tool:,apple: -s -k "$KC_PASS" Keys.keychain
  fi

  ART_DIR="$PROJECT_ROOT/Builds/xcode/${GAME}_artefacts/Release"
  ART_DIR_ED="$PROJECT_ROOT/Builds/xcode/${EDITOR}_artefacts/Release"

  cd "$PROJECT_ROOT"
  cmake --preset xcode
  cmake --build --preset xcode --config Release            # game
  cmake --build --preset xcode --config Release --target "$EDITOR"

  STAGE="$PROJECT_ROOT/Installer/macOS/bin/stage"
  PKG_DIR="$PROJECT_ROOT/Installer/macOS/bin/pkgs"
  rm -Rf "$STAGE" "$PKG_DIR"
  mkdir -p "$STAGE/app"
  mkdir -p "$PKG_DIR"

  cp -RL "$ART_DIR/$GAME.app" "$STAGE/app/"
  cp -RL "$ART_DIR_ED/$EDITOR_APP.app" "$STAGE/app/"

  if security find-identity -v -p codesigning | grep -q "$DEV_APP_ID"; then
    codesign -s "$DEV_APP_ID" --options=runtime --timestamp --force --deep -v "$STAGE/app/$GAME.app"
    codesign -s "$DEV_APP_ID" --options=runtime --timestamp --force --deep -v "$STAGE/app/$EDITOR_APP.app"
  else
    echo "Skipping codesign — Developer ID Application not in keychain"
  fi

  # Component package — installs Shunt.app to /Applications
  pkgbuild --root "$STAGE/app" \
           --install-location "/Applications" \
           --identifier "${BUNDLE_BASE}.app.pkg" \
           --version "$VERSION" \
           --scripts "$PROJECT_ROOT/Installer/macOS/scripts" \
           "$PKG_DIR/app.pkg"

  # Maps component — the shipped levels, installed to the shared folder the game
  # scans and the editor writes; a postinstall makes it world-writable.
  MAPS_ROOT="$PROJECT_ROOT/Installer/macOS/bin/maps-root"
  rm -Rf "$MAPS_ROOT"
  mkdir -p "$MAPS_ROOT"
  cp "$PROJECT_ROOT/Maps/"*.json "$MAPS_ROOT/"

  pkgbuild --root "$MAPS_ROOT" \
           --install-location "/Library/Application Support/Shunt/Maps" \
           --identifier "${BUNDLE_BASE}.maps.pkg" \
           --version "$VERSION" \
           --scripts "$PROJECT_ROOT/Installer/macOS/maps-scripts" \
           "$PKG_DIR/maps.pkg"

  cp "$PROJECT_ROOT/Installer/macOS/welcome.txt" "$PKG_DIR/welcome.txt"

  PKG_OUT="$PROJECT_ROOT/Installer/macOS/bin/${GAME}.pkg"

  productbuild --distribution "$PROJECT_ROOT/Installer/macOS/distribution.xml" \
               --package-path "$PKG_DIR" \
               --resources "$PKG_DIR" \
               --version "$VERSION" \
               "$PKG_OUT.unsigned"

  if security find-identity -v | grep -q "$DEV_INST_ID"; then
    productsign --sign "$DEV_INST_ID" "$PKG_OUT.unsigned" "$PKG_OUT"
    rm "$PKG_OUT.unsigned"
  else
    echo "Skipping productsign — Developer ID Installer not in keychain"
    mv "$PKG_OUT.unsigned" "$PKG_OUT"
  fi

  if [ -n "${APPLE_USER:-}" ] && [ -n "${APPLE_PASS:-}" ]; then
    xcrun notarytool submit --verbose \
      --apple-id "$APPLE_USER" --password "$APPLE_PASS" --team-id "$TEAM_ID" \
      --wait --timeout 30m "$PKG_OUT"
    xcrun stapler staple "$PKG_OUT"
  else
    echo "Skipping notarization — APPLE_USER / APPLE_PASS not set"
  fi

  cp "$PKG_OUT" "$PROJECT_ROOT/bin/"

  # Symbols zip
  if [ -d "$ART_DIR/$GAME.app.dSYM" ]; then
    cd "$ART_DIR"
    zip -r "$PROJECT_ROOT/bin/Symbols_Mac.zip" "$GAME.app.dSYM"
  fi

############################################################
# Linux — single executable, tar.gz (no native installer)
############################################################
elif [ "$PLATFORM" = "linux" ]; then
  cd "$PROJECT_ROOT"
  cmake --preset ninja-gcc
  cmake --build --preset ninja-gcc --config Release            # game
  cmake --build --preset ninja-gcc --config Release --target "$EDITOR"

  ART_DIR="$PROJECT_ROOT/Builds/ninja-gcc/${GAME}_artefacts/Release"
  ART_DIR_ED="$PROJECT_ROOT/Builds/ninja-gcc/${EDITOR}_artefacts/Release"

  STAGE="$PROJECT_ROOT/Installer/linux/bin/stage/$GAME"
  rm -Rf "$STAGE"
  mkdir -p "$STAGE"

  cp "$ART_DIR/$GAME" "$STAGE/"
  strip "$STAGE/$GAME"
  cp "$ART_DIR_ED/$EDITOR_APP" "$STAGE/"
  strip "$STAGE/$EDITOR_APP"

  # Ship the maps next to the binary; the app seeds its per-user folder from here.
  mkdir -p "$STAGE/Maps"
  cp "$PROJECT_ROOT/Maps/"*.json "$STAGE/Maps/"

  cd "$PROJECT_ROOT/Installer/linux/bin/stage"
  tar -czf "$PROJECT_ROOT/bin/${GAME}_Linux.tar.gz" "$GAME"

############################################################
# Windows — Inno Setup, install to Program Files + Azure signing
############################################################
else
  cd "$PROJECT_ROOT"
  cmake --preset vs
  cmake --build --preset vs --config Release            # game
  cmake --build --preset vs --config Release --target "$EDITOR"

  ART_DIR="$PROJECT_ROOT/Builds/vs/${GAME}_artefacts/Release"
  ART_DIR_ED="$PROJECT_ROOT/Builds/vs/${EDITOR}_artefacts/Release"
  STAGE="$PROJECT_ROOT/Installer/win/bin"

  rm -Rf "$STAGE"
  mkdir -p "$STAGE/app"
  cp "$ART_DIR/$GAME.exe" "$STAGE/app/"
  cp "$ART_DIR_ED/$EDITOR_APP.exe" "$STAGE/app/"

  #
  # Sign the executable via Microsoft Trusted Signing.
  # Required env: AZURE_TENANT_ID, AZURE_CLIENT_ID, AZURE_CLIENT_SECRET.
  #
  uuid_re='^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$'
  WIN_SIGN=0
  if [ -n "${AZURE_TENANT_ID:-}" ] || [ -n "${AZURE_CLIENT_ID:-}" ] || [ -n "${AZURE_CLIENT_SECRET:-}" ]; then
    if   [[ ! "${AZURE_TENANT_ID:-}" =~ $uuid_re ]]; then
      echo "ERROR: AZURE_TENANT_ID is not a valid GUID — set the secret on the repo or unset all three to skip signing."; exit 1
    elif [[ ! "${AZURE_CLIENT_ID:-}" =~ $uuid_re ]]; then
      echo "ERROR: AZURE_CLIENT_ID is not a valid GUID — set the secret on the repo or unset all three to skip signing."; exit 1
    elif [ -z "${AZURE_CLIENT_SECRET:-}" ]; then
      echo "ERROR: AZURE_CLIENT_SECRET is empty — set the secret on the repo or unset all three to skip signing."; exit 1
    else
      WIN_SIGN=1
    fi
  fi

  if [ "$WIN_SIGN" = "1" ]; then
    SIGNTOOL=$(ls -1 "/c/Program Files (x86)/Windows Kits/10/bin/"*/x64/signtool.exe 2>/dev/null | sort | tail -1)
    if [ -z "$SIGNTOOL" ]; then
      echo "signtool.exe not found in Windows Kits"; exit 1
    fi

    TOOLS_DIR="$STAGE/_signingtools"
    mkdir -p "$TOOLS_DIR"
    nuget install Microsoft.Trusted.Signing.Client -Version 1.0.86 \
                  -OutputDirectory "$TOOLS_DIR" -ExcludeVersion -NonInteractive
    DLIB="$TOOLS_DIR/Microsoft.Trusted.Signing.Client/bin/x64/Azure.CodeSigning.Dlib.dll"
    METADATA="$PROJECT_ROOT/Installer/win/metadata.json"

    sign_file () {
      "$SIGNTOOL" sign -v -fd SHA256 \
        -tr "http://timestamp.acs.microsoft.com" -td SHA256 \
        -dlib "$DLIB" -dmdf "$METADATA" "$1"
    }

    sign_file "$STAGE/app/$GAME.exe"
    sign_file "$STAGE/app/$EDITOR_APP.exe"
  else
    echo "Skipping Windows binary signing — Azure secrets not set"
  fi

  # Build installer .exe
  cd "$PROJECT_ROOT/Installer/win"
  ISCC="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
  if [ ! -f "$ISCC" ]; then
    ISCC="/c/Program Files/Inno Setup 6/ISCC.exe"
  fi
  # Note the leading `//`: MSYS Git Bash on Windows would otherwise convert
  # `/D...` into the Windows path `D:\...` because anything starting with /<X>
  # is treated as a drive-letter Unix path. `//D...` survives intact as `/D...`
  # by the time ISCC.exe gets it.
  "$ISCC" "//DGameVersion=$VERSION" "$PROJECT_ROOT/Installer/win/Shunt.iss"

  EXE_OUT="$PROJECT_ROOT/Installer/win/bin/${GAME}.exe"

  if [ "$WIN_SIGN" = "1" ]; then
    sign_file "$EXE_OUT"
  fi

  cp "$EXE_OUT" "$PROJECT_ROOT/bin/"
fi

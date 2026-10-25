#!/usr/bin/env bash
# Build a distributable zip for players (Windows or Wine): out/dist/SporeCoop-<ver>.zip
# Contents are extracted into SporebinEP1 and installed with install.bat.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER="$(grep -oP 'define COOP_VERSION "\K[^"]+' "$ROOT/mods/coop/coop.cpp")"
STAGE="$ROOT/out/dist/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/SporeCoop/mods/sporekit/symbols"
cp "$ROOT/build/steam_api.dll" "$STAGE/SporeCoop/steam_api.sporekit.dll"
cp "$ROOT/build/sporekit.dll" "$STAGE/SporeCoop/sporekit.dll"
cp "$ROOT/build/coop.dll" "$STAGE/SporeCoop/mods/coop.dll"
cp "$ROOT"/data/symbols/*.sym "$STAGE/SporeCoop/mods/sporekit/symbols/"
cp "$ROOT"/data/config/*.ini "$STAGE/SporeCoop/mods/"
cp "$ROOT/build/SporeCoop.package" "$STAGE/SporeCoop/SporeCoop.package"  # menu icon (Spore loads Data\*.package)

cat > "$STAGE/install.bat" <<'EOF'
@echo off
rem SporeCoopGA installer: run from the SporebinEP1 folder of SPORE (GOG or Steam)
cd /d "%~dp0"
if not exist SporeApp.exe (
  echo Put install.bat and the SporeCoop folder into ...\SPORE\SporebinEP1 and run it again.
  pause
  exit /b 1
)
rem supported builds: GOG 2016 (922af7c7...), GOG 2024 (dc04aee5...) and Steam (3010a413..., the GOG 2024
rem code under Steam's DRM), INSTALL.txt section 2
set WANT1=922af7c7908af2614b9caca2ffb1ec2353442022fd124866105724acaa24c26e
set WANT2=dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37
set WANT3=3010a413f9a4a05ee9c4de24756c1c94c7fb218f70cad29bcf5bc0b4abfa2466
for /f "usebackq delims=" %%h in (`powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 SporeApp.exe).Hash.ToLower()" 2^>nul`) do set HAVE=%%h
if defined HAVE if /i not "%HAVE%"=="%WANT1%" if /i not "%HAVE%"=="%WANT2%" if /i not "%HAVE%"=="%WANT3%" (
  echo Note: this SporeApp.exe is not one of the builds SporeCoop supports.
  echo It has not been tested yet - see INSTALL.txt, section 2.
  echo.
)
if /i "%HAVE%"=="%WANT2%" (
  echo Exact GOG 2024 executable: co-op Galactic Adventures preview is available.
) else (
  echo Native co-op adventures require the exact GOG 2024 executable.
  echo Steam support is retained for base stages; Steam co-op adventures are unvalidated.
)
if not exist steam_api_orig.dll (
  copy /y steam_api.dll steam_api_orig.dll >nul
)
copy /y SporeCoop\steam_api.sporekit.dll steam_api.dll >nul
copy /y SporeCoop\sporekit.dll sporekit.dll >nul
if not exist mods mkdir mods
xcopy /e /i /y /q SporeCoop\mods mods >nul
copy /y SporeCoop\SporeCoop.package ..\Data\SporeCoop.package >nul
echo SporeCoop installed. Start SPORE as usual.
echo Main menu: "Co-op" - host a game or join a friend. In game F9 opens the co-op window.
pause
EOF

cat > "$STAGE/uninstall.bat" <<'EOF'
@echo off
cd /d "%~dp0"
if exist steam_api_orig.dll (
  copy /y steam_api_orig.dll steam_api.dll >nul
  del steam_api_orig.dll
)
del /q sporekit.dll 2>nul
del /q ..\Data\SporeCoop.package 2>nul
echo SporeCoop removed (the mods folder was kept).
pause
EOF

cat > "$STAGE/README-SporeCoop.txt" <<EOF
SporeCoopGA $VER - Galactic Adventures preview on SporeCoop 0.7.2.1
https://github.com/jwhans0n/SporeCoopGA
Based on SporeCoop by WoodyWizard: https://github.com/WoodyWizard/SporeCoop

Install: extract this zip into SPORE\\SporebinEP1 and run install.bat (step by step: INSTALL.txt).
Read GALACTIC-ADVENTURES.md for the adventure preview's verified cases and limits.
All players need Galactic Adventures and this exact SporeCoopGA version (protocol 14).
Upstream SporeCoop 0.7.0-0.7.2.1 uses a different protocol and cannot join this preview.
Client creatures and vehicles follow native waypoints; the host retains AI,
combat and objective authority. Authored player-team attack targets can recognize
eligible guest captains; native sensing, hostility and target selection remain
in control. Targeted two-player Mine guest/host targeting, natural damage and
switching checks pass; broader adventure compatibility remains unproven. See
validation-ga-preview.11.md for evidence and limits. Earlier follower results
retain residual movement lag. Ordinary guest click/key combat input remains
unverified.

Base-stage support retains upstream GOG 2024 (sha256 dc04aee5...), Steam
(3010a413...: GOG 2024 game code under Steam's DRM), and GOG 2016 (922af7c7...).
Upstream testing: GOG 2016/2024 on Linux Wine, GOG 2024 on Windows 11, and Steam
offline on Windows/Proton. See INSTALL.txt section 2 for the precise historical scope.
EA app and disc remain unvalidated unless they match an existing supported build.

The native co-op adventure adapter requires the exact GOG SporeApp.exe SHA-256:
dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37.
This is the executable upstream calls GOG 2024, called March 2017 in the GA RE notes.
Steam and other same-code variants retain base-stage support but are not enabled
for the native GA adapter. Adventures start from Sporepedia; Space-stage entry
is outside this preview's scope.

Host: main menu -> Co-op -> name -> Host. The window shows addresses for friends.
Internet play needs UDP port 47810 forwarding or a VPN such as Tailscale/ZeroTier.
Guest: main menu -> Co-op -> name -> click the host on your network or type its
address -> Join. Join before the adventure attempt begins.
Host: choose Adventure settings, open an adventure in Sporepedia and press Play.
Each player chooses or creates, names and saves a captain, then reviews the
shared adventure/captain cards and explicitly presses Ready after transfer.
After loading and beam-down, every player presses Continue before clocks start.
F9 opens party/chat controls in game. Late joins wait for the next attempt.
For other stages, the host loads a save or starts a game and shares that world.

Fork releases and problem reports: https://github.com/jwhans0n/SporeCoopGA
Updates use published releases from this fork and skip prereleases. While no
release exists, download sporecoop-zip from the latest successful main Actions
build: https://github.com/jwhans0n/SporeCoopGA/actions/workflows/build.yml
Extract it and keep SporeCoopSetup.exe beside its matching .pack file.
Cheat console (Ctrl+Shift+C): coop host | coop join <ip> | coop leave.
EOF
# the toolchain the Makefile uses (LLVM_MINGW ?= ... there), unless set in the environment
LLVM_MINGW="${LLVM_MINGW:-$(sed -n 's/^LLVM_MINGW ?= //p' "$ROOT/Makefile" | sed 's/\$(HOME)/$HOME/; s|\$HOME|'"$HOME"'|')}"
STRIP="$LLVM_MINGW/bin/llvm-strip"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
cp "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/THIRD_PARTY_NOTICES.txt"
cp "$ROOT/third_party/minhook/LICENSE.txt" "$STAGE/SporeCoop/MinHook-LICENSE.txt"
cp "$ROOT/docs/INSTALL.md" "$STAGE/INSTALL.txt"
cp "$ROOT/docs/GALACTIC-ADVENTURES.md" "$STAGE/GALACTIC-ADVENTURES.md"
cp "$ROOT/docs/validation-ga-preview.11.md" "$STAGE/validation-ga-preview.11.md"
cp "$ROOT/docs/validation-ga-preview.10.md" "$STAGE/validation-ga-preview.10.md"
cp "$ROOT/docs/validation-ga-preview.9.md" "$STAGE/validation-ga-preview.9.md"
cp "$ROOT/docs/validation-ga-preview.8.md" "$STAGE/validation-ga-preview.8.md"
cp "$ROOT/docs/integration-ga-upstream-0.7.2.1.md" "$STAGE/integration-ga-upstream-0.7.2.1.md"
mkdir -p "$STAGE/re"
cp "$ROOT"/docs/re/{adventure,adventure-world,creature,build-dc04}.md "$STAGE/re/"
"$STRIP" "$STAGE/SporeCoop/sporekit.dll" "$STAGE/SporeCoop/mods/coop.dll" "$STAGE/SporeCoop/steam_api.sporekit.dll"
# the update pack (in-game updates and the setup) and the setup itself, also next to the zip
python3 "$ROOT/tools/mkpack.py" "$STAGE" "$VER" "$STAGE/SporeCoop-$VER.pack" >/dev/null
cp "$ROOT/build/SporeCoopSetup.exe" "$STAGE/SporeCoopSetup.exe"
"$STRIP" "$STAGE/SporeCoopSetup.exe"
cp "$STAGE/SporeCoop-$VER.pack" "$STAGE/SporeCoopSetup.exe" "$ROOT/out/dist/"
sed -i 's/\r*$/\r/' "$STAGE"/*.bat "$STAGE"/*.txt  # Windows line endings for cmd.exe and Notepad
if command -v zip >/dev/null 2>&1; then
    (cd "$STAGE" && rm -f "../SporeCoop-$VER.zip" && zip -qr "../SporeCoop-$VER.zip" .)
else
    python3 - "$STAGE" "$ROOT/out/dist/SporeCoop-$VER.zip" <<'PY'
from pathlib import Path
import sys
import zipfile
stage, target = map(Path, sys.argv[1:])
with zipfile.ZipFile(target, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            archive.write(path, path.relative_to(stage).as_posix())
PY
fi
echo "$ROOT/out/dist/SporeCoop-$VER.zip"

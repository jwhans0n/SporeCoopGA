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
rem SporeCoop installer: run from the SporebinEP1 folder of SPORE (GOG)
cd /d "%~dp0"
if not exist SporeApp.exe (
  echo Put install.bat and the SporeCoop folder into ...\SPORE\SporebinEP1 and run it again.
  pause
  exit /b 1
)
rem Exact hashes have different verified scopes; see GALACTIC-ADVENTURES.md.
set WANT=922af7c7908af2614b9caca2ffb1ec2353442022fd124866105724acaa24c26e
set GA=dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37
for /f "usebackq delims=" %%h in (`powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 SporeApp.exe).Hash.ToLower()" 2^>nul`) do set HAVE=%%h
set KNOWN=
if /i "%HAVE%"=="%GA%" (
  set KNOWN=1
  echo GOG March 2017: Galactic Adventures preview. Other stages are unvalidated.
)
if /i "%HAVE%"=="%WANT%" (
  set KNOWN=1
  echo Previously tested GOG build. The native GA preview adapter needs March 2017.
)
if defined HAVE if not defined KNOWN (
  echo This executable is unvalidated and its native GA preview adapter is unavailable.
  echo See GALACTIC-ADVENTURES.md and INSTALL.txt, section 2.
  echo.
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
SporeCoop $VER - Galactic Adventures preview
https://github.com/WoodyWizard/SporeCoop

Install: extract this zip into SPORE\\SporebinEP1 and run install.bat (step by step: INSTALL.txt).
Read GALACTIC-ADVENTURES.md for the preview's setup, verified cases and limits.
All players need Galactic Adventures and this exact SporeCoop preview version.

Native GA adapter: GOG March 2017 SporebinEP1\\SporeApp.exe, SHA-256
dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37.
This preview is for adventures started from Sporepedia. Entry from the Space stage
is outside its scope. Other stages on this executable have not been validated.
The native GA adapter is unavailable on other executable hashes.

Earlier SporeCoop stage testing used the older GOG executable, SHA-256
922af7c7908af2614b9caca2ffb1ec2353442022fd124866105724acaa24c26e.
Those historical stage results do not establish GA preview support on that build.

Host: main menu -> Co-op -> name -> Host. The window shows the
addresses for friends (over the internet forward UDP port 47810 or use a VPN such as
ZeroTier / Radmin VPN / Tailscale).
Guest: main menu -> Co-op -> name -> click the host under "Games on your network" or type
its address -> Join. Join before the adventure begins.
Host: choose Adventure settings, then open an adventure in Sporepedia and press Play.
Each player chooses a saved captain and confirms readiness after content transfer.
The party starts after everyone has the matching content and loaded adventure.
In game F9 opens the co-op window. Late joins and reconnects wait for the next attempt.
Install future preview builds manually; the updater does not order preview suffixes.
Cheat console (Ctrl+Shift+C) works too: coop host | coop join <ip> | coop leave.
EOF
# the toolchain the Makefile uses (LLVM_MINGW ?= ... there), unless set in the environment
LLVM_MINGW="${LLVM_MINGW:-$(sed -n 's/^LLVM_MINGW ?= //p' "$ROOT/Makefile" | sed 's/\$(HOME)/$HOME/; s|\$HOME|'"$HOME"'|')}"
STRIP="$LLVM_MINGW/bin/llvm-strip"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
cp "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/THIRD_PARTY_NOTICES.txt"
cp "$ROOT/third_party/minhook/LICENSE.txt" "$STAGE/SporeCoop/MinHook-LICENSE.txt"
cp "$ROOT/docs/INSTALL.md" "$STAGE/INSTALL.txt"
cp "$ROOT/docs/GALACTIC-ADVENTURES.md" "$STAGE/GALACTIC-ADVENTURES.md"
mkdir -p "$STAGE/re"
cp "$ROOT"/docs/re/{adventure,adventure-world,build-dc04}.md "$STAGE/re/"
"$STRIP" "$STAGE/SporeCoop/sporekit.dll" "$STAGE/SporeCoop/mods/coop.dll" "$STAGE/SporeCoop/steam_api.sporekit.dll"
# the update pack (in-game updates and the setup) and the setup itself, also next to the zip
python3 "$ROOT/tools/mkpack.py" "$STAGE" "$VER" "$STAGE/SporeCoop-$VER.pack" >/dev/null
cp "$ROOT/build/SporeCoopSetup.exe" "$STAGE/SporeCoopSetup.exe"
"$STRIP" "$STAGE/SporeCoopSetup.exe"
cp "$STAGE/SporeCoop-$VER.pack" "$STAGE/SporeCoopSetup.exe" "$ROOT/out/dist/"
sed -i 's/\r*$/\r/' "$STAGE"/*.bat "$STAGE"/*.txt  # Windows line endings for cmd.exe and Notepad
(cd "$STAGE" && rm -f "../SporeCoop-$VER.zip" && zip -qr "../SporeCoop-$VER.zip" .)
echo "$ROOT/out/dist/SporeCoop-$VER.zip"

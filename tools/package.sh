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

# install.bat / uninstall.bat (installer/): install.bat keeps the player's .ini files (BUG-0080)
cp "$ROOT/installer/install.bat" "$ROOT/installer/uninstall.bat" "$STAGE/"
cp "$ROOT/installer/ini.exclude" "$STAGE/SporeCoop/ini.exclude"  # xcopy /exclude: names with ".ini"

cat > "$STAGE/README-SporeCoop.txt" <<EOF
SporeCoopGA $VER - Experimental Galactic Adventures co-op on SporeCoop 0.7.3.6
https://github.com/jwhans0n/SporeCoopGA
Based on SporeCoop by WoodyWizard: https://github.com/WoodyWizard/SporeCoop

Install: extract this zip into SPORE\\SporebinEP1 and run install.bat (step by step: INSTALL.txt).
Release: https://github.com/jwhans0n/SporeCoopGA/releases/tag/v0.7.4.0
Read GALACTIC-ADVENTURES.md for the adventure release's verified cases and limits.
All players need Galactic Adventures and this exact SporeCoopGA version (protocol 15).
Upstream SporeCoop 0.7.0-0.7.2.1 uses a different protocol and cannot join this release.
Client creatures and vehicles follow native waypoints; the host retains AI,
combat and objective authority. Authored player-team attack targets can recognize
eligible guest captains; native sensing, hostility and target selection remain
in control. Shared wins/losses stop gameplay for the party. Host Done returns
everyone to the menu; guest Done accepts results locally. Replay requires host
Replay and every remaining player to apply and accept results, then opens a
shared lobby with fresh explicit readiness. A downed captain's 1 HP display permits revival, not
continued play after a shared loss. See validation-ga-0.7.4.0.md for release checks
and validation-ga-preview.12.md for its native acceptance evidence and limits.
Gameplay and protocol are retained from preview 12. Historical preview 11 Mine guest/host targeting,
natural damage and switching checks remain in validation-ga-preview.11.md.
Broader adventure compatibility remains unproven. Earlier follower results
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
is outside this release's scope.

Host: main menu -> Co-op -> name -> Host. The window shows addresses for friends.
Internet play needs UDP port 47810 forwarding or a VPN such as Tailscale/ZeroTier.
Guest: main menu -> Co-op -> name -> click the host on your network or type its
address -> Join. Join before the adventure attempt begins.
Host: choose Adventure settings, open an adventure in Sporepedia and press Play.
Each player chooses or creates, names and saves a captain, then reviews the
shared adventure/captain cards and explicitly presses Ready after transfer.
After loading and beam-down, every player presses Continue before clocks start.
F9 opens party/chat controls in game. Late joins wait for the next attempt.
Host Done keeps the party connected and cancels a pending Replay. Review the
shared lobby and press Ready again when starting a Replay attempt.
Finish mandatory native captain reward selections before a party exit completes;
the normal native captain-save flow is preserved.
For other stages, the host loads a save or starts a game and shares that world.

Fork releases and problem reports: https://github.com/jwhans0n/SporeCoopGA
Download the release zip, extract it and keep SporeCoopSetup.exe beside its
matching .pack file. Updates use published releases from this fork and skip
prereleases. Version 0.7.4.0 sorts above 0.7.3.5-ga-preview.12 for the updater;
Galactic Adventures support remains experimental.
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
cp "$ROOT/docs/validation-ga-0.7.4.0.md" "$STAGE/validation-ga-0.7.4.0.md"
cp "$ROOT/docs/validation-ga-preview.12.md" "$STAGE/validation-ga-preview.12.md"
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
sed -i 's/\r*$/\r/' "$STAGE"/*.bat "$STAGE"/*.txt "$STAGE/SporeCoop/ini.exclude"  # Windows line endings for cmd.exe and Notepad
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

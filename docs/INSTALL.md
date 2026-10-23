# Installing SporeCoop

For `0.7.0-ga-preview.1`, read [the Galactic Adventures preview guide](GALACTIC-ADVENTURES.md)
first. Its native adventure adapter is verified only on the GOG March 2017
executable (`dc04aee5...`); other stages on that executable remain unvalidated.
The earlier stage compatibility results below apply to the older GOG executable.

Every player installs the same SporeCoop version into their own copy of the game. The install
replaces one file of the game (`steam_api.dll`, kept as `steam_api_orig.dll`) and adds a few
others; your saves and creations are not touched.

* [1. What you need](#1-what-you-need)
* [2. Check your game build](#2-check-your-game-build)
* [3. Install on Windows](#3-install-on-windows)
* [4. Install on Linux (Wine, Bottles, Lutris, Proton)](#4-install-on-linux-wine-bottles-lutris-proton)
* [5. Install by hand](#5-install-by-hand)
* [6. Check that it works](#6-check-that-it-works)
* [7. Playing over the internet](#7-playing-over-the-internet)
* [8. Update and uninstall](#8-update-and-uninstall)
* [9. Troubleshooting](#9-troubleshooting)

## 1. What you need

| | |
|---|---|
| Game | **Spore + Galactic Adventures**; tested with the **SPORE Collection from GOG**. The mod runs the Galactic Adventures executable `SporebinEP1\SporeApp.exe`. |
| System | Windows 10/11, or Linux with Wine 9+ (Bottles, Lutris, plain Wine; Proton works if the game runs from a GOG install). |
| Players | 2–4, every player with the **same SporeCoop version** (the network protocol is checked on join). |
| Network | The same local network, a virtual LAN (Tailscale, ZeroTier, Radmin VPN), or UDP port 47810 forwarded to the host. |

The mod is tested with the GOG build. Other builds (Steam, EA app, disc) have not been tested
yet; if you try one, an [issue](https://github.com/WoodyWizard/SporeCoop/issues) with the result
and `mods\logs\sporekit.log` helps.

## 2. Check your game build

The older build used for previous stage testing is this file:

| | |
|---|---|
| File | `SPORE\SporebinEP1\SporeApp.exe` |
| Size | 24 888 320 bytes |
| SHA-256 | `922af7c7908af2614b9caca2ffb1ec2353442022fd124866105724acaa24c26e` |

The Galactic Adventures preview uses a separate native adapter for GOG March 2017:

| | |
|---|---|
| File | `SPORE\SporebinEP1\SporeApp.exe` |
| Size | 24 895 536 bytes |
| SHA-256 | `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37` |
| Verified scope | Two-player adventures started from Sporepedia; see the preview guide. Other stages on this executable are unvalidated. |

Where the game usually is:

* GOG Galaxy: `C:\Program Files (x86)\GOG Galaxy\Games\SPORE`
* GOG offline installer: `C:\GOG Games\SPORE`
* Linux: inside the Wine prefix, e.g. `drive_c/GOG Games/SPORE`, or wherever you installed it.

Windows (PowerShell, in the `SporebinEP1` folder):

```powershell
Get-FileHash .\SporeApp.exe -Algorithm SHA256
```

Linux:

```bash
sha256sum "SPORE/SporebinEP1/SporeApp.exe"
```

`install.bat` checks this too and tells you when your build is not the tested one.

## 3. Install on Windows

### With the setup (recommended)

1. Download **`SporeCoopSetup.exe`** from the
   [latest release](https://github.com/WoodyWizard/SporeCoop/releases/latest) and run it.
2. It finds the game by itself (GOG and EA registry entries, common folders); if not, press
   **Browse...** and choose the SPORE folder (the one with `SporebinEP1`).
3. It shows whether your game build is the tested one and which SporeCoop version is installed.
   Press **Install**. The setup downloads the newest release from GitHub and checks it.
4. If the game is under `C:\Program Files (x86)`, the setup offers to restart itself as
   administrator (Windows does not let normal programs write there).
5. Start SPORE as usual.

The same window updates SporeCoop (**Update to x.y.z**) and removes it (**Uninstall**). Without
internet, run the `SporeCoopSetup.exe` from the release zip: it installs the update pack
(`SporeCoop-<version>.pack`) lying next to it.

### With install.bat

1. Download `SporeCoop-<version>.zip` from the
   [latest release](https://github.com/WoodyWizard/SporeCoop/releases/latest).
2. Open the game folder and go into **`SporebinEP1`** (the folder with `SporeApp.exe`).
3. Extract the zip there, so that `SporebinEP1` now contains `install.bat` and a `SporeCoop` folder.
4. Run **`install.bat`**. If the game is under `C:\Program Files (x86)`, right-click it and choose
   **Run as administrator** (Windows does not let normal programs write there).
5. Start SPORE as usual (from GOG Galaxy or the desktop shortcut).

Some antivirus programs warn about a replaced `steam_api.dll`: that file is the SporeCoop
loader. Its source is in this repository (`loader/loader.c`); the release is built by
GitHub Actions from that source.

## 4. Install on Linux (Wine, Bottles, Lutris, Proton)

The files are the same as on Windows. The easiest way: run **`SporeCoopSetup.exe`** in the game's
Wine prefix (Bottles: *Run Executable...*; Lutris: *Run EXE inside Wine prefix*; plain Wine:
`WINEPREFIX=~/.wine wine SporeCoopSetup.exe`). If it does not find the game, point **Browse...** at
the SPORE folder inside the prefix.

Or run `install.bat`, one of these ways:

**Plain Wine** (use your prefix):

```bash
cd "/path/to/SPORE/SporebinEP1"
unzip ~/Downloads/SporeCoop-*.zip
WINEPREFIX=~/.wine wine cmd /c install.bat
```

**Bottles:** open the bottle that has SPORE → *Run Executable…* → choose `install.bat` in
`SporebinEP1`. Or from a terminal:
`flatpak run --command=bottles-cli com.usebottles.bottles run -b "<bottle>" -e "/path/to/SPORE/SporebinEP1/install.bat"`

**Lutris:** right-click the game → *Run EXE inside Wine prefix* → `install.bat`.

**Without running anything:** do the [install by hand](#5-install-by-hand) with your file manager.

`steam_api.dll` is loaded from the game folder, so no DLL override is needed.

## 5. Install by hand

Inside `SporebinEP1`, after extracting the zip:

| Do | From | To |
|---|---|---|
| rename (once, keep it!) | `steam_api.dll` | `steam_api_orig.dll` |
| copy | `SporeCoop\steam_api.sporekit.dll` | `steam_api.dll` |
| copy | `SporeCoop\sporekit.dll` | `sporekit.dll` |
| copy the folder contents | `SporeCoop\mods\` | `mods\` |
| copy | `SporeCoop\SporeCoop.package` | `..\Data\SporeCoop.package` |

The result:

```
SPORE\
├── Data\SporeCoop.package        the "Co-op" menu icon and notification icons
└── SporebinEP1\
    ├── SporeApp.exe              the game (unchanged)
    ├── steam_api.dll             SporeCoop loader
    ├── steam_api_orig.dll        the original steam_api.dll (the loader passes calls to it)
    ├── sporekit.dll              SporeKit: symbols, hooks, console
    └── mods\
        ├── coop.dll              the co-op mod
        ├── coop.ini, sporekit.ini
        ├── sporekit\symbols\*.sym   game function addresses
        └── logs\                 created on the first start
```

## 6. Check that it works

* The main menu (galaxy screen) has a new **Co-op** item on the left.
* `SporebinEP1\mods\logs\sporekit.log` exists and reports the installed version
  (for this preview, `loaded coop v0.7.0-ga-preview.1`).
* Start a game, press **F9**: the co-op window opens.

If there is no Co-op item, see [Troubleshooting](#9-troubleshooting).

## 7. Playing over the internet

The host listens on **UDP port 47810**. Guests need to reach it:

* **Easiest:** all players join the same virtual LAN:
  [Tailscale](https://tailscale.com), [ZeroTier](https://www.zerotier.com) or
  [Radmin VPN](https://www.radmin-vpn.com). Then join with the host's VPN address
  (the co-op window shows the host's addresses), or pick the host under *Games on your network*.
* **Port forwarding:** forward UDP 47810 on the host's router to the host's PC, allow
  SporeApp.exe in the Windows firewall, and give friends your public IP.

Connections across a long distance work (the protocol was tested with 120 ms ± 60 ms delay and
3 % packet loss); very lossy links can make a rejoin slow (see `docs/known-issues.md`).

## 8. Update and uninstall

**Update in the game:** after the start SporeCoop asks GitHub whether a newer release exists. If so,
a notification says so and the **Co-op** window shows an **Update** button: the new files are
downloaded, checked against the release's SHA-256 sums and installed; restart Spore to use them.
All players update together (the network protocol has to match).

The check sends one request to `api.github.com` per start and nothing else. To turn it off, set
in `SporebinEP1\mods\coop.ini`:

```ini
[update]
check=0
```

**Update with the setup:** run `SporeCoopSetup.exe` and press **Update to x.y.z**. **By hand:**
extract the new zip into `SporebinEP1` over the old files and run `install.bat` again.

**Uninstall:** in `SporeCoopSetup.exe` press **Uninstall**, or run `uninstall.bat` in `SporebinEP1`. It puts the original `steam_api.dll` back and
removes `sporekit.dll` and `Data\SporeCoop.package`; delete the `mods` folder too if you like.
By hand: delete `steam_api.dll`, rename `steam_api_orig.dll` back to `steam_api.dll`, delete
`sporekit.dll`, `mods\` and `..\Data\SporeCoop.package`.

**Your saves:** a guest stores the host's world as a separate save slot (marked as a co-op world)
and reuses it next time. If you already have your own save of the same planet, it is moved to
`Games-sporecoop-backup` next to your `Games` folder first, never overwritten.

## 9. Troubleshooting

| Problem | What to do |
|---|---|
| No **Co-op** item in the menu | Check the build (section 2). Check that `steam_api.dll` in `SporebinEP1` is the SporeCoop one (about 8 KB) and `steam_api_orig.dll` exists. Look at `mods\logs\sporekit.log`. |
| The game does not start after installing | Run `uninstall.bat`, start the game once without the mod, then install again. An antivirus may have removed `steam_api.dll` or `sporekit.dll`: restore them or add an exception. |
| *Games on your network* stays empty | Both PCs must be on the same network or VPN; allow SporeApp.exe in the firewall (private networks). Type the host's address instead. |
| Join fails or times out | The host's UDP 47810 must be reachable (VPN or port forwarding). Every player needs the same SporeCoop version. |
| The guest loads the world and then nothing | In Space the host must be in a star system, not on a planet (the game only saves off-planet). |
| Something else | Open an [issue](https://github.com/WoodyWizard/SporeCoop/issues) with `mods\logs\sporekit.log` of host and guest and what you did. |
